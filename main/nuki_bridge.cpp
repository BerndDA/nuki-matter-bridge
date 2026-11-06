#include "nuki_bridge.h"

#include <atomic>

#include <Arduino.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "BleScanner.h"
#include "NukiConstants.h"
#include "NukiLock.h"
#include "NukiLockConstants.h"

static const char *TAG = "nuki";

namespace nuki_bridge {
namespace {

constexpr int kCommandRetries = 3;
constexpr uint32_t kLoopDelayMs = 50;
constexpr uint32_t kPairingRetryDelayMs = 500;
constexpr uint32_t kFailedReadRetryMs = 5000;

BleScanner::Scanner *s_scanner = nullptr;
NukiLock::NukiLock *s_lock = nullptr;
QueueHandle_t s_cmdQueue = nullptr;
StatusCallback s_onStatus = nullptr;

std::atomic<bool> s_paired{false};
std::atomic<bool> s_unpairRequested{false};
std::atomic<bool> s_stateDirty{true};  // read the state once at startup
BridgeStatus s_lastStatus;

// Called by the Nuki library (BLE host task) when the lock's beacon signals a
// state change. Only flag it here; the BLE read happens in our own task.
class EventHandler : public Nuki::SmartlockEventHandler {
public:
    void notify(Nuki::EventType eventType) override
    {
        if (eventType == Nuki::EventType::KeyTurnerStatusUpdated) {
            s_stateDirty = true;
        } else if (eventType == Nuki::EventType::ERROR_BAD_PIN) {
            ESP_LOGW(TAG, "Lock reported a bad security PIN");
        }
    }
};
EventHandler s_eventHandler;

BridgeLockState mapLockState(NukiLock::LockState s)
{
    using LS = NukiLock::LockState;
    switch (s) {
    case LS::Locked:       return BridgeLockState::Locked;
    case LS::Unlocked:
    case LS::UnlockedLnga: return BridgeLockState::Unlocked;
    case LS::Unlatched:    return BridgeLockState::Unlatched;
    case LS::Locking:      return BridgeLockState::Locking;
    case LS::Unlocking:
    case LS::Unlatching:   return BridgeLockState::Unlocking;
    case LS::MotorBlocked: return BridgeLockState::Jammed;
    case LS::Uncalibrated:
    case LS::Calibration:  return BridgeLockState::Uncalibrated;
    default:               return BridgeLockState::Unknown;  // BootRun, Undefined
    }
}

BridgeDoorState mapDoorState(Nuki::DoorSensorState s)
{
    using DS = Nuki::DoorSensorState;
    switch (s) {
    case DS::DoorClosed: return BridgeDoorState::Closed;
    case DS::DoorOpened: return BridgeDoorState::Open;
    case DS::Tampered:
    case DS::DoorStateUnknown: return BridgeDoorState::Error;
    default: return BridgeDoorState::NotAvailable;
    }
}

NukiLock::LockAction mapCommand(BridgeLockCommand cmd)
{
    switch (cmd) {
    case BridgeLockCommand::Lock:    return NukiLock::LockAction::Lock;
    case BridgeLockCommand::Unlatch: return NukiLock::LockAction::Unlatch;
    case BridgeLockCommand::Unlock:
    default:                         return NukiLock::LockAction::Unlock;
    }
}

bool readState()
{
    NukiLock::KeyTurnerState kts;
    Nuki::CmdResult r = s_lock->requestKeyTurnerState(&kts);
    if (r != Nuki::CmdResult::Success) {
        ESP_LOGW(TAG, "requestKeyTurnerState failed: %d", (int)r);
        return false;
    }

    BridgeStatus st;
    st.lock = mapLockState(kts.lockState);
    st.door = mapDoorState(kts.doorSensorState);
    st.batteryPercent = s_lock->getBatteryPerc();
    st.batteryCritical = s_lock->isBatteryCritical();
    st.valid = true;

    ESP_LOGI(TAG, "state: nuki lock=0x%02x door=0x%02x battery=%u%%%s", (unsigned)kts.lockState,
             (unsigned)kts.doorSensorState, st.batteryPercent, st.batteryCritical ? " (critical)" : "");

    s_lastStatus = st;
    if (s_onStatus) {
        s_onStatus(st);
    }
    return true;
}

void executeCommand(BridgeLockCommand cmd)
{
    NukiLock::LockAction action = mapCommand(cmd);
    for (int attempt = 1; attempt <= kCommandRetries; ++attempt) {
        Nuki::CmdResult r = s_lock->lockAction(action, CONFIG_NUKI_APP_ID, 0);
        if (r == Nuki::CmdResult::Success) {
            ESP_LOGI(TAG, "lock action 0x%02x ok", (unsigned)action);
            break;
        }
        ESP_LOGW(TAG, "lock action 0x%02x failed (%d), attempt %d/%d", (unsigned)action, (int)r, attempt,
                 kCommandRetries);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    // Always re-read: on success it reports the motor movement, on failure it
    // reverts Matter to the real state instead of leaving "locking..." hanging.
    s_stateDirty = true;
}

void onPaired()
{
    s_paired = true;
    s_scanner->whitelist(s_lock->getBleAddress());
    s_lock->setEventHandler(&s_eventHandler);
    s_stateDirty = true;
}

void task(void *)
{
    s_scanner = new BleScanner::Scanner();
    // Continuous scanning with duplicates: the lock signals state changes via its beacon.
    s_scanner->initialize(CONFIG_NUKI_DEVICE_NAME, true, 40, 40);
    s_scanner->setScanDuration(0);

    s_lock = new NukiLock::NukiLock(CONFIG_NUKI_DEVICE_NAME, CONFIG_NUKI_DEVICE_ID);
    s_lock->initialize();
    s_lock->registerBleScanner(s_scanner);
    s_lock->setConnectTimeout(3);
    s_lock->setDisconnectTimeout(2000);
    s_lock->setGeneralTimeout(10000);
    s_lock->setCommandTimeout(3000);

    if (s_lock->isPairedWithLock()) {
        ESP_LOGI(TAG, "using stored pairing with %s", s_lock->getBleAddress().toString().c_str());
        onPaired();
    } else {
        ESP_LOGW(TAG, "not paired - press the lock's button for 5 s to enter pairing mode");
    }

    const int64_t pollIntervalUs = (int64_t)CONFIG_NUKI_STATE_POLL_INTERVAL_S * 1000000;
    int64_t nextPollUs = 0;

    for (;;) {
        s_scanner->update();

        if (s_unpairRequested.exchange(false)) {
            s_lock->unPairNuki();
            s_paired = false;
            ESP_LOGW(TAG, "unpaired from lock");
        }

        if (!s_paired) {
            if (s_lock->pairNuki(Nuki::AuthorizationIdType::Bridge) == Nuki::PairingResult::Success) {
                ESP_LOGI(TAG, "paired with %s", s_lock->getBleAddress().toString().c_str());
                onPaired();
            } else {
                vTaskDelay(pdMS_TO_TICKS(kPairingRetryDelayMs));
                continue;
            }
        }

        BridgeLockCommand cmd;
        if (xQueueReceive(s_cmdQueue, &cmd, 0) == pdTRUE) {
            executeCommand(cmd);
        }

        const int64_t now = esp_timer_get_time();
        if (s_stateDirty.exchange(false) || now >= nextPollUs) {
            if (readState()) {
                nextPollUs = now + pollIntervalUs;
            } else {
                nextPollUs = now + (int64_t)kFailedReadRetryMs * 1000;
            }
        }

        s_lock->updateConnectionState();
        vTaskDelay(pdMS_TO_TICKS(kLoopDelayMs));
    }
}

} // namespace

void start(StatusCallback onStatus)
{
    s_onStatus = onStatus;
    s_cmdQueue = xQueueCreate(4, sizeof(BridgeLockCommand));
    // Pinned to the core NimBLE runs on; the Nuki lib is not thread-safe otherwise.
    xTaskCreatePinnedToCore(task, "nuki", 8192, nullptr, 5, nullptr, 0);
}

bool enqueueCommand(BridgeLockCommand cmd)
{
    if (!s_cmdQueue || !s_paired) {
        return false;
    }
    return xQueueSend(s_cmdQueue, &cmd, 0) == pdTRUE;
}

bool isPaired()
{
    return s_paired;
}

void unpair()
{
    s_unpairRequested = true;
}

} // namespace nuki_bridge
