// Simulated Nuki lock (CONFIG_NUKI_SIMULATE): same interface as nuki_bridge.cpp,
// no BLE. Lets the Matter side be commissioned and tested without a lock in range.

#include <sdkconfig.h>

#if CONFIG_NUKI_SIMULATE

#include "nuki_bridge.h"

#include <cstdio>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

static const char *TAG = "nuki-sim";

namespace nuki_bridge {
namespace {

constexpr uint32_t kMotorTimeMs = 1500;

QueueHandle_t s_cmdQueue = nullptr;
StatusCallback s_onStatus = nullptr;
InfoCallback s_onInfo = nullptr;
BridgeStatus s_status;

void report()
{
    ESP_LOGI(TAG, "state: lock=%d battery=%u%%", (int)s_status.lock, s_status.batteryPercent);
    if (s_onStatus) {
        s_onStatus(s_status);
    }
}

void task(void *)
{
    vTaskDelay(pdMS_TO_TICKS(2000));  // like a first BLE read after boot
    report();
    if (s_onInfo) {
        BridgeDeviceInfo info;
        snprintf(info.serial, sizeof(info.serial), "SIM00001");
        snprintf(info.name, sizeof(info.name), "Simulated Nuki");
        snprintf(info.firmware, sizeof(info.firmware), "0.0.0");
        snprintf(info.hardware, sizeof(info.hardware), "0.0");
        info.valid = true;
        s_onInfo(info);
    }

    for (;;) {
        BridgeLockCommand cmd;
        if (xQueueReceive(s_cmdQueue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        const bool locking = cmd == BridgeLockCommand::Lock;
        ESP_LOGI(TAG, "command %d", (int)cmd);

        s_status.lock = locking ? BridgeLockState::Locking : BridgeLockState::Unlocking;
        report();
        vTaskDelay(pdMS_TO_TICKS(kMotorTimeMs));

        switch (cmd) {
        case BridgeLockCommand::Lock:    s_status.lock = BridgeLockState::Locked; break;
        case BridgeLockCommand::Unlock:  s_status.lock = BridgeLockState::Unlocked; break;
        case BridgeLockCommand::Unlatch: s_status.lock = BridgeLockState::Unlatched; break;
        }
        if (s_status.batteryPercent > 0) {
            s_status.batteryPercent--;
        }
        s_status.batteryCritical = s_status.batteryPercent < 20;
        report();

        if (cmd == BridgeLockCommand::Unlatch) {  // a real Nuki falls back to unlocked
            vTaskDelay(pdMS_TO_TICKS(3000));
            s_status.lock = BridgeLockState::Unlocked;
            report();
        }
    }
}

} // namespace

void start(StatusCallback onStatus, InfoCallback onInfo)
{
    s_onInfo = onInfo;
    ESP_LOGW(TAG, "SIMULATION MODE - no Nuki lock is used");
    s_onStatus = onStatus;
    s_status.lock = BridgeLockState::Locked;
    s_status.door = BridgeDoorState::Closed;
    s_status.batteryPercent = 80;
    s_status.valid = true;
    s_cmdQueue = xQueueCreate(4, sizeof(BridgeLockCommand));
    xTaskCreate(task, "nuki_sim", 3072, nullptr, 5, nullptr);
}

bool enqueueCommand(BridgeLockCommand cmd)
{
    return s_cmdQueue && xQueueSend(s_cmdQueue, &cmd, 0) == pdTRUE;
}

bool isPaired()
{
    return true;
}

void setScanningPaused(bool)
{
}

void unpair()
{
    ESP_LOGW(TAG, "unpair (simulated)");
}

} // namespace nuki_bridge

#endif // CONFIG_NUKI_SIMULATE
