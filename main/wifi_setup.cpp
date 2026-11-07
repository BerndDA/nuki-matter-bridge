#include "wifi_setup.h"

#include <cstring>

#include <esp_log.h>
#include <esp_matter.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ESP32/NetworkCommissioningDriver.h>

static const char *TAG = "wifi";

namespace wifi_setup {

namespace {

constexpr uint32_t kWatchdogPeriodMs = 5000;
constexpr uint32_t kNoIpTimeoutMs = 60000;

void applyLocked()
{
    const char *ssid = CONFIG_NUKI_WIFI_SSID;
    const char *pass = CONFIG_NUKI_WIFI_PASSWORD;

    if (chip::DeviceLayer::ConnectivityMgr().IsWiFiStationProvisioned()) {
        ESP_LOGI(TAG, "using stored Wi-Fi credentials");
        return;
    }
    if (strlen(ssid) == 0) {
        ESP_LOGW(TAG, "no Wi-Fi credentials. Set them in menuconfig or run: matter esp wifi connect <ssid> <password>");
        return;
    }

    ESP_LOGI(TAG, "connecting to '%s'", ssid);
    CHIP_ERROR err = chip::DeviceLayer::NetworkCommissioning::ESPWiFiDriver::GetInstance().ConnectWiFiNetwork(
        ssid, (uint8_t)strlen(ssid), pass, (uint8_t)strlen(pass));
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "ConnectWiFiNetwork failed: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

// CHIP stores networks added via the Network Commissioning driver (incl. `matter esp wifi
// connect`) with the default fast scan: the first AP found with the SSID wins. With a
// repeater/mesh that can be a weak one where DHCP never completes. Switch the stored config
// to "scan all channels, strongest AP"; it persists in the Wi-Fi NVS, so this runs once.
void preferStrongestAp()
{
    wifi_config_t cfg = {};
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK || cfg.sta.ssid[0] == 0) {
        return;
    }
    if (cfg.sta.scan_method == WIFI_ALL_CHANNEL_SCAN && cfg.sta.sort_method == WIFI_CONNECT_AP_BY_SIGNAL) {
        return;
    }
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    esp_wifi_disconnect();  // set_config is refused while connecting; CHIP reconnects on its own
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi set to connect to the strongest AP; reconnecting");
    } else {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: 0x%x", err);
    }
}

bool hasIpv4()
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip = {};
    return netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0;
}

// Safety net: associated with an AP but no IPv4 address for a minute (DHCP failing) ->
// drop the link so CHIP reconnects, picking the strongest AP again.
void watchdogTask(void *)
{
    uint32_t noIpMs = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(kWatchdogPeriodMs));
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK || hasIpv4()) {
            noIpMs = 0;
            continue;
        }
        noIpMs += kWatchdogPeriodMs;
        if (noIpMs >= kNoIpTimeoutMs) {
            ESP_LOGW(TAG, "no IPv4 address after %u s on %02x:%02x:%02x:%02x:%02x:%02x (rssi %d): reconnecting",
                     (unsigned)(noIpMs / 1000), ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4],
                     ap.bssid[5], ap.rssi);
            esp_wifi_disconnect();
            noIpMs = 0;
        }
    }
}

} // namespace

void applyDefaultCredentials()
{
    auto status = esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    applyLocked();
    preferStrongestAp();
    if (status == esp_matter::lock::SUCCESS) {
        esp_matter::lock::chip_stack_unlock();
    }
    xTaskCreate(watchdogTask, "wifi_wd", 2048, nullptr, 2, nullptr);
}

} // namespace wifi_setup
