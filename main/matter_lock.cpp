#include "matter_lock.h"

#include <cinttypes>
#include <cstring>

#include <esp_log.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include <esp_matter.h>

#include <app/clusters/door-lock-server/door-lock-server.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <platform/CHIPDeviceLayer.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include "chip_lock.h"
#include "nuki_bridge.h"
#include "wifi_setup.h"

static const char *TAG = "matter";

using namespace esp_matter;
using namespace chip::app::Clusters;
using chip::app::DataModel::Nullable;

namespace {

uint16_t s_lockEndpointId = 0;

// Copy of the last reported status for the web UI (written by the Nuki task).
portMUX_TYPE s_snapshotMux = portMUX_INITIALIZER_UNLOCKED;
BridgeStatus s_snapshot;
bool s_haveSnapshot = false;

constexpr auto kCommissioningWindowTimeout = chip::System::Clock::Seconds16(300);
BridgeLockState s_lastLockState = BridgeLockState::Unknown;

#if CONFIG_NUKI_OPEN_ENDPOINT
// Second Door Lock endpoint "Open": Unlock = Nuki unlatch. It rests in Locked, shows
// Unlocked briefly once the Nuki has unlatched, then returns to Locked.
// All of this runs on the CHIP task (command callbacks, applyStatus, timers).
uint16_t s_openEndpointId = chip::kInvalidEndpointId;
bool s_openPending = false;  // unlatch requested via this endpoint, not yet seen
constexpr auto kOpenShowUnlocked = chip::System::Clock::Seconds32(3);
constexpr auto kOpenTimeout = chip::System::Clock::Seconds32(30);

void openRelock(chip::System::Layer *, void *)
{
    s_openPending = false;
    DoorLockServer::Instance().SetLockState(s_openEndpointId, DlLockState::kLocked);
}

void openShowUnlocked()
{
    s_openPending = false;
    chip::DeviceLayer::SystemLayer().CancelTimer(openRelock, nullptr);
    DoorLockServer::Instance().SetLockState(s_openEndpointId, DlLockState::kUnlocked);
    chip::DeviceLayer::SystemLayer().StartTimer(kOpenShowUnlocked, openRelock, nullptr);
}

bool isOpenEndpoint(chip::EndpointId ep)
{
    return ep == s_openEndpointId;
}
#endif

// After an OTA update the new image boots as "pending verify"; the bootloader rolls back to
// the previous one on the next reset unless it is confirmed. Reaching Wi-Fi with an IP is
// the criterion: then the bridge can at least receive another update.
void confirmFirmware()
{
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "new firmware confirmed, rollback cancelled");
    }
}

void onDeviceEvent(const ChipDeviceEvent *event, intptr_t)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "IP address changed");
        if (esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")) {
            esp_netif_ip_info_t ip = {};
            if (esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0) {
                ESP_LOGI(TAG, "web UI: http://" IPSTR "/", IP2STR(&ip.ip));
                confirmFirmware();
            }
        }
        break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        // Last controller removed: re-open the commissioning window (over DNS-SD,
        // since BLE is reserved for the Nuki link).
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            auto &mgr = chip::Server::GetInstance().GetCommissioningWindowManager();
            if (!mgr.IsCommissioningWindowOpen()) {
                mgr.OpenBasicCommissioningWindow(kCommissioningWindowTimeout,
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

#if CONFIG_NUKI_OPEN_ENDPOINT
    // Unlatched (from any source, e.g. the Nuki app) or our own request completed.
    // Unlocked counts too: a fast Nuki can be past Unlatched before the next read.
    if (st.lock == BridgeLockState::Unlatched || (s_openPending && st.lock == BridgeLockState::Unlocked)) {
        openShowUnlocked();
    }
#endif

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

bool emberAfPluginDoorLockOnDoorLockCommand(chip::EndpointId endpoint, const Nullable<chip::FabricIndex> &,
                                            const Nullable<chip::NodeId> &, const chip::Optional<chip::ByteSpan> &,
                                            OperationErrorEnum &err)
{
#if CONFIG_NUKI_OPEN_ENDPOINT
    if (isOpenEndpoint(endpoint)) {  // nothing to lock: just return to the resting state
        ESP_LOGI(TAG, "Matter: Lock on 'Open' -> no-op");
        chip::DeviceLayer::SystemLayer().CancelTimer(openRelock, nullptr);
        openRelock(nullptr, nullptr);
        return true;
    }
#endif
    ESP_LOGI(TAG, "Matter: Lock");
    return submit(BridgeLockCommand::Lock, err);
}

// With the Unbolting feature, Matter's UnlockDoor means "unlock and pull the latch".
bool emberAfPluginDoorLockOnDoorUnlockCommand(chip::EndpointId endpoint, const Nullable<chip::FabricIndex> &,
                                              const Nullable<chip::NodeId> &, const chip::Optional<chip::ByteSpan> &,
                                              OperationErrorEnum &err)
{
#if CONFIG_NUKI_OPEN_ENDPOINT
    if (isOpenEndpoint(endpoint)) {
        ESP_LOGI(TAG, "Matter: Unlock on 'Open' -> Nuki unlatch");
        if (!submit(BridgeLockCommand::Unlatch, err)) {
            return false;
        }
        s_openPending = true;
        // If the Nuki never reports the unlatch, fall back to Locked.
        chip::DeviceLayer::SystemLayer().CancelTimer(openRelock, nullptr);
        chip::DeviceLayer::SystemLayer().StartTimer(kOpenTimeout, openRelock, nullptr);
        return true;
    }
#endif
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

namespace {

void setString(endpoint_t *ep, uint32_t clusterId, uint32_t attrId, char *value)
{
    attribute_t *attr = attribute::get(cluster::get(ep, clusterId), attrId);
    esp_matter_attr_val_t v = esp_matter_char_str(value, strlen(value));
    if (!attr || attribute::set_val(attr, &v) != ESP_OK) {
        ESP_LOGW(TAG, "could not set attribute 0x%04" PRIx32 " on endpoint %u", attrId, endpoint::get_id(ep));
    }
}

// A lock as a bridged device below the aggregator. Apple Home names bridged devices by
// their NodeLabel; plain endpoints of one device all appear as "Matter Accessory".
endpoint_t *createLockEndpoint(node_t *node, endpoint_t *aggregator, char *label, char *uniqueId,
                               DoorLock::DlLockType type, nullable<uint8_t> initialState)
{
    endpoint::bridged_node::config_t bridgedCfg;
    endpoint_t *ep = endpoint::bridged_node::create(node, &bridgedCfg, ENDPOINT_FLAG_BRIDGE, nullptr);
    if (!ep || endpoint::set_parent_endpoint(ep, aggregator) != ESP_OK) {
        ESP_LOGE(TAG, "failed to create bridged endpoint");
        return nullptr;
    }
    cluster_t *bdbi = cluster::get(ep, BridgedDeviceBasicInformation::Id);
    cluster::bridged_device_basic_information::attribute::create_node_label(bdbi, label, strlen(label));
    setString(ep, BridgedDeviceBasicInformation::Id, BridgedDeviceBasicInformation::Attributes::UniqueID::Id,
              uniqueId);

    endpoint::door_lock::config_t cfg;
    cfg.door_lock.lock_type = chip::to_underlying(type);
    cfg.door_lock.lock_state = initialState;
    cfg.door_lock.actuator_enabled = true;
    if (endpoint::door_lock::add(ep, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "failed to add door lock to endpoint %u", endpoint::get_id(ep));
        return nullptr;
    }
    // The server reads AutoRelockTime after every unlock. 0 = disabled: the Nuki's own
    // auto-lock (configured in the Nuki app) handles relocking.
    cluster::door_lock::attribute::create_auto_relock_time(cluster::get(ep, DoorLock::Id), 0);
    return ep;
}

} // namespace

namespace matter_lock {

esp_err_t start()
{
    // Labels must stay valid until the attributes are stored, so keep them static.
    static char bridgeLabel[] = CONFIG_NUKI_MATTER_BRIDGE_LABEL;
    static char lockLabel[] = CONFIG_NUKI_MATTER_LOCK_LABEL;
    static char lockUid[] = "nuki-" CONFIG_NUKI_DEVICE_NAME "-lock";

    node::config_t nodeConfig;
    strncpy(nodeConfig.root_node.basic_information.node_label, bridgeLabel,
            sizeof(nodeConfig.root_node.basic_information.node_label) - 1);
    node_t *node = node::create(&nodeConfig, onAttributeUpdate, onIdentify);
    if (!node) {
        ESP_LOGE(TAG, "failed to create node");
        return ESP_FAIL;
    }

    // Endpoint IDs are assigned in creation order (esp_matter only loads its persisted
    // counter in esp_matter::start), so this order fixes: 1 = aggregator,
    // 2 = lock, 3 = "Open". Keep it stable, controllers key on endpoint IDs.
    endpoint::aggregator::config_t aggCfg;
    endpoint_t *aggregator = endpoint::aggregator::create(node, &aggCfg, ENDPOINT_FLAG_NONE, nullptr);
    if (!aggregator) {
        ESP_LOGE(TAG, "failed to create aggregator");
        return ESP_FAIL;
    }

    endpoint_t *ep = createLockEndpoint(node, aggregator, lockLabel, lockUid, DoorLock::DlLockType::kDeadBolt,
                                        nullable<uint8_t>());
    if (!ep) {
        return ESP_FAIL;
    }
    s_lockEndpointId = endpoint::get_id(ep);

    [[maybe_unused]] cluster_t *lockCluster = cluster::get(ep, DoorLock::Id);
#if CONFIG_NUKI_ENABLE_UNBOLT
    cluster::door_lock::feature::unbolting::add(lockCluster);
#endif
#if CONFIG_NUKI_DOOR_SENSOR
    cluster::door_lock::feature::door_position_sensor::add(lockCluster);
#endif

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

#if CONFIG_NUKI_OPEN_ENDPOINT
    static char openLabel[] = CONFIG_NUKI_MATTER_OPEN_LABEL;
    static char openUid[] = "nuki-" CONFIG_NUKI_DEVICE_NAME "-open";
    endpoint_t *openEp = createLockEndpoint(node, aggregator, openLabel, openUid, DoorLock::DlLockType::kLatchBolt,
                                            nullable<uint8_t>(chip::to_underlying(DlLockState::kLocked)));
    if (!openEp) {
        return ESP_FAIL;
    }
    s_openEndpointId = endpoint::get_id(openEp);
    ESP_LOGI(TAG, "'Open' (unlatch) lock on endpoint %u", s_openEndpointId);
#endif

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
    taskENTER_CRITICAL(&s_snapshotMux);
    s_snapshot = status;
    s_haveSnapshot = true;
    taskEXIT_CRITICAL(&s_snapshotMux);

    auto *copy = new BridgeStatus(status);
    if (chip::DeviceLayer::PlatformMgr().ScheduleWork(applyStatusWork, reinterpret_cast<intptr_t>(copy)) !=
        CHIP_NO_ERROR) {
        delete copy;
    }
}

bool lastStatus(BridgeStatus &out)
{
    taskENTER_CRITICAL(&s_snapshotMux);
    out = s_snapshot;
    bool have = s_haveSnapshot;
    taskEXIT_CRITICAL(&s_snapshotMux);
    return have;
}

esp_err_t openCommissioningWindow()
{
    ChipLock lock;
    auto &mgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    if (mgr.IsCommissioningWindowOpen()) {
        return ESP_OK;
    }
    CHIP_ERROR err = mgr.OpenBasicCommissioningWindow(kCommissioningWindowTimeout,
                                                      chip::CommissioningWindowAdvertisement::kDnssdOnly);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "opening commissioning window failed: %" CHIP_ERROR_FORMAT, err.Format());
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "commissioning window opened");
    return ESP_OK;
}

void factoryReset()
{
    esp_matter::factory_reset();
}

} // namespace matter_lock
