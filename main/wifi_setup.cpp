#include "wifi_setup.h"

#include <cstring>

#include <esp_log.h>
#include <esp_matter.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ESP32/NetworkCommissioningDriver.h>

static const char *TAG = "wifi";

namespace wifi_setup {

static void applyLocked()
{
    const char *ssid = CONFIG_NUKI_WIFI_SSID;
    const char *pass = CONFIG_NUKI_WIFI_PASSWORD;

    if (chip::DeviceLayer::ConnectivityMgr().IsWiFiStationProvisioned()) {
        ESP_LOGI(TAG, "using stored Wi-Fi credentials");
        return;
    }
    if (strlen(ssid) == 0) {
        ESP_LOGW(TAG, "no Wi-Fi credentials. Set them in menuconfig or run: matter wifi connect <ssid> <password>");
        return;
    }

    ESP_LOGI(TAG, "connecting to '%s'", ssid);
    CHIP_ERROR err = chip::DeviceLayer::NetworkCommissioning::ESPWiFiDriver::GetInstance().ConnectWiFiNetwork(
        ssid, (uint8_t)strlen(ssid), pass, (uint8_t)strlen(pass));
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "ConnectWiFiNetwork failed: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

void applyDefaultCredentials()
{
    auto status = esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    applyLocked();
    if (status == esp_matter::lock::SUCCESS) {
        esp_matter::lock::chip_stack_unlock();
    }
}

} // namespace wifi_setup
