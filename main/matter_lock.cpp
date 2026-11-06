#include "matter_lock.h"

#include <cstring>

#include <esp_log.h>
#include <esp_matter.h>

#include <app/clusters/door-lock-server/door-lock-server.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <platform/CHIPDeviceLayer.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include "nuki_bridge.h"
#include "wifi_setup.h"

static const char *TAG = "matter";

using namespace esp_matter;
using namespace chip::app::Clusters;
using chip::app::DataModel::Nullable;

namespace {

uint16_t s_lockEndpointId = 0;
BridgeLockState s_lastLockState = BridgeLockState::Unknown;

void onDeviceEvent(const ChipDeviceEvent *event, intptr_t)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "IP address changed");
        break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        // Last controller removed: re-open the commissioning window (over DNS-SD,
        // since BLE is reserved for the Nuki link).
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            auto &mgr = chip::Server::GetInstance().GetCommissioningWindowManager();
            if (!mgr.IsCommissioningWindowOpen()) {
                mgr.OpenBasicCommissioningWindow(chip::System::Clock::Seconds16(300),
                                                 chip::CommissioningWindowAdvertisement::kDnssdOnly);
            }
        }
        break;
    default:
        break;
    }
}

esp_err_t onAttributeUpdate(attribute::callback_type_t, uint16_t, uint32_t, uint32_t, esp_matter_attr_val_t *, void *)
{
    return ESP_OK;  // lock state is driven by the Nuki side, nothing to do here
}

esp_err_t onIdentify(identification::callback_type_t, uint16_t, uint8_t, uint8_t, void *)
{
    return ESP_OK;
}

bool submit(BridgeLockCommand cmd, OperationErrorEnum &err)
{
    if (!nuki_bridge::enqueueCommand(cmd)) {
        ESP_LOGW(TAG, "rejecting command: Nuki not paired or queue full");
        err = OperationErrorEnum::kUnspecified;
        return false;
    }
    // Accepted. LockState is updated once the lock reports the new state.
    return true;
}

// ---- runs on the CHIP task ------------------------------------------------

void applyStatus(const BridgeStatus &st)
{
    auto &dls = DoorLockServer::Instance();

    // Keep the previous LockState while the motor runs; Matter has no
    // "locking"/"unlocking" value and controllers show progress on their own.
    switch (st.lock) {
    // The 2-argument SetLockState only writes the attribute; LockOperation
    // events for manual/keypad/app use aren't emitted (source not tracked).
    case BridgeLockState::Locked:    dls.SetLockState(s_lockEndpointId, DlLockState::kLocked); break;
    case BridgeLockState::Unlocked:  dls.SetLockState(s_lockEndpointId, DlLockState::kUnlocked); break;
#if CONFIG_NUKI_ENABLE_UNBOLT
    case BridgeLockState::Unlatched: dls.SetLockState(s_lockEndpointId, DlLockState::kUnlatched); break;
#else
    case BridgeLockState::Unlatched: dls.SetLockState(s_lockEndpointId, DlLockState::kUnlocked); break;
#endif
    case BridgeLockState::Jammed:    dls.SetLockState(s_lockEndpointId, DlLockState::kNotFullyLocked); break;
    case BridgeLockState::Locking:
    case BridgeLockState::Unlocking: break;
    default: {  // unknown / uncalibrated -> null
        esp_matter_attr_val_t v = esp_matter_nullable_enum8(nullable<uint8_t>());
        attribute::update(s_lockEndpointId, DoorLock::Id, DoorLock::Attributes::LockState::Id, &v);
        break;
    }
    }

    if (st.lock == BridgeLockState::Jammed && s_lastLockState != BridgeLockState::Jammed) {
        dls.SendLockAlarmEvent(s_lockEndpointId, AlarmCodeEnum::kLockJammed);
    }
    s_lastLockState = st.lock;

#if CONFIG_NUKI_DOOR_SENSOR
    switch (st.door) {
    case BridgeDoorState::Open:   dls.SetDoorState(s_lockEndpointId, DoorStateEnum::kDoorOpen); break;
    case BridgeDoorState::Closed: dls.SetDoorState(s_lockEndpointId, DoorStateEnum::kDoorClosed); break;
    case BridgeDoorState::Error:  dls.SetDoorState(s_lockEndpointId, DoorStateEnum::kDoorUnspecifiedError); break;
    default: break;
    }
#endif

    // PowerSource.BatPercentRemaining is in half-percent units (0..200)
    esp_matter_attr_val_t pct = esp_matter_nullable_uint8(nullable<uint8_t>(uint8_t(st.batteryPercent * 2)));
    attribute::update(s_lockEndpointId, PowerSource::Id, PowerSource::Attributes::BatPercentRemaining::Id, &pct);

    // BatChargeLevelEnum: 0 = OK, 2 = critical
    esp_matter_attr_val_t lvl = esp_matter_enum8(st.batteryCritical ? 2 : 0);
    attribute::update(s_lockEndpointId, PowerSource::Id, PowerSource::Attributes::BatChargeLevel::Id, &lvl);

    esp_matter_attr_val_t repl = esp_matter_bool(st.batteryCritical);
    attribute::update(s_lockEndpointId, PowerSource::Id, PowerSource::Attributes::BatReplacementNeeded::Id, &repl);
}

void applyStatusWork(intptr_t arg)
{
    auto *st = reinterpret_cast<BridgeStatus *>(arg);
    applyStatus(*st);
    delete st;
}

} // namespace

// ---- Door Lock cluster callbacks (override weak defaults in the CHIP SDK) ----

void emberAfDoorLockClusterInitCallback(chip::EndpointId endpoint)
{
    // LockState starts as null (unknown) from the endpoint config until the first read.
    DoorLockServer::Instance().InitServer(endpoint);
}

bool emberAfPluginDoorLockOnDoorLockCommand(chip::EndpointId, const Nullable<chip::FabricIndex> &,
                                            const Nullable<chip::NodeId> &, const chip::Optional<chip::ByteSpan> &,
                                            OperationErrorEnum &err)
{
    ESP_LOGI(TAG, "Matter: Lock");
    return submit(BridgeLockCommand::Lock, err);
}

// With the Unbolting feature, Matter's UnlockDoor means "unlock and pull the latch".
bool emberAfPluginDoorLockOnDoorUnlockCommand(chip::EndpointId, const Nullable<chip::FabricIndex> &,
                                              const Nullable<chip::NodeId> &, const chip::Optional<chip::ByteSpan> &,
                                              OperationErrorEnum &err)
{
#if CONFIG_NUKI_ENABLE_UNBOLT
    ESP_LOGI(TAG, "Matter: Unlock -> Nuki unlatch");
    return submit(BridgeLockCommand::Unlatch, err);
#else
    ESP_LOGI(TAG, "Matter: Unlock");
    return submit(BridgeLockCommand::Unlock, err);
#endif
}

bool emberAfPluginDoorLockOnDoorUnboltCommand(chip::EndpointId, const Nullable<chip::FabricIndex> &,
                                              const Nullable<chip::NodeId> &, const chip::Optional<chip::ByteSpan> &,
                                              OperationErrorEnum &err)
{
    ESP_LOGI(TAG, "Matter: Unbolt -> Nuki unlock");
    return submit(BridgeLockCommand::Unlock, err);
}

namespace matter_lock {

esp_err_t start()
{
    node::config_t nodeConfig;
    node_t *node = node::create(&nodeConfig, onAttributeUpdate, onIdentify);
    if (!node) {
        ESP_LOGE(TAG, "failed to create node");
        return ESP_FAIL;
    }

    endpoint::door_lock::config_t lockConfig;
    lockConfig.door_lock.lock_type = chip::to_underlying(DoorLock::DlLockType::kDeadBolt);
    lockConfig.door_lock.lock_state = nullable<uint8_t>();  // unknown until first read
    lockConfig.door_lock.actuator_enabled = true;
    endpoint_t *ep = endpoint::door_lock::create(node, &lockConfig, ENDPOINT_FLAG_NONE, nullptr);
    if (!ep) {
        ESP_LOGE(TAG, "failed to create door lock endpoint");
        return ESP_FAIL;
    }
    s_lockEndpointId = endpoint::get_id(ep);

    cluster_t *lockCluster = cluster::get(ep, DoorLock::Id);
#if CONFIG_NUKI_ENABLE_UNBOLT
    cluster::door_lock::feature::unbolting::add(lockCluster);
#endif
#if CONFIG_NUKI_DOOR_SENSOR
    cluster::door_lock::feature::door_position_sensor::add(lockCluster);
#endif
    (void)lockCluster;

    // Battery reporting
    cluster::power_source::config_t psConfig;
    psConfig.status = chip::to_underlying(PowerSource::PowerSourceStatusEnum::kActive);
    strncpy(psConfig.description, "Battery", sizeof(psConfig.description) - 1);
    psConfig.features.battery.bat_replaceability =
        chip::to_underlying(PowerSource::BatReplaceabilityEnum::kUserReplaceable);
    psConfig.feature_flags = cluster::power_source::feature::battery::get_id();
    cluster_t *ps = cluster::power_source::create(ep, &psConfig, CLUSTER_FLAG_SERVER);
    cluster::power_source::attribute::create_bat_percent_remaining(ps, nullable<uint8_t>(), nullable<uint8_t>(0),
                                                                    nullable<uint8_t>(200));

    ESP_LOGI(TAG, "door lock on endpoint %u", s_lockEndpointId);

    esp_err_t err = esp_matter::start(onDeviceEvent);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_matter::start failed: %d", err);
        return err;
    }

    // BLE belongs to the Nuki link, so advertise on-network discovery in the QR code.
    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kOnNetwork));

    wifi_setup::applyDefaultCredentials();
    return ESP_OK;
}

void publishStatus(const BridgeStatus &status)
{
    auto *copy = new BridgeStatus(status);
    if (chip::DeviceLayer::PlatformMgr().ScheduleWork(applyStatusWork, reinterpret_cast<intptr_t>(copy)) !=
        CHIP_NO_ERROR) {
        delete copy;
    }
}

void factoryReset()
{
    esp_matter::factory_reset();
}

} // namespace matter_lock
