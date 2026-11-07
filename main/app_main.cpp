// Nuki Smart Lock -> Matter (over Wi-Fi) bridge.
//
//   Nuki lock  <--BLE (NukiBleEsp32)-->  ESP32  <--Wi-Fi / Matter-->  Apple Home, Google Home, Home Assistant, ...
//
// BLE is used exclusively for the Nuki link, so Matter commissioning uses the
// on-network (DNS-SD) path: the bridge must already be on your Wi-Fi.

#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_matter_console.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "matter_lock.h"
#include "nuki_bridge.h"
#include "web_ui.h"

static const char *TAG = "app";

// initArduino() frees the BLE controller memory unless bleInUse() says otherwise.
// Arduino's own BLE library normally overrides this; we use esp-nimble-cpp instead.
// (Classic BT memory is still released, which we don't need.)
extern "C" bool bleInUse(void)
{
    return true;
}

// initArduino() would confirm a freshly OTA-updated image right away. We confirm it ourselves
// once Wi-Fi has an IP (matter_lock.cpp: confirmFirmware), so an update that can't get on the
// network is rolled back by the bootloader on the next reset.
extern "C" bool verifyRollbackLater(void)
{
    return true;
}

static void resetButtonTask(void *)
{
    const gpio_num_t pin = (gpio_num_t)CONFIG_NUKI_RESET_GPIO;
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << pin;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&cfg);

    uint32_t heldMs = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        heldMs = gpio_get_level(pin) == 0 ? heldMs + 100 : 0;
        if (heldMs == 10000) {
            ESP_LOGW(TAG, "reset button held: unpairing Nuki and factory-resetting Matter");
            nuki_bridge::unpair();
            vTaskDelay(pdMS_TO_TICKS(1000));  // let the Nuki task delete its credentials
            matter_lock::factoryReset();      // reboots
        }
    }
}

extern "C" void app_main()
{
    initArduino();  // NVS, Preferences, millis()/delay() used by the Nuki library

    if (matter_lock::start() != ESP_OK) {
        ESP_LOGE(TAG, "Matter failed to start");
        return;
    }

    nuki_bridge::start(matter_lock::publishStatus, matter_lock::publishInfo);
    web_ui::start();

#if CONFIG_NUKI_RESET_GPIO >= 0
    xTaskCreate(resetButtonTask, "reset_btn", 3072, nullptr, 2, nullptr);
#endif

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::init();
#endif
}
