#pragma once

namespace wifi_setup {

// If the device has no stored Wi-Fi credentials and CONFIG_NUKI_WIFI_SSID is set,
// hands them to the Matter Wi-Fi driver (which stores them). Call after esp_matter::start().
void applyDefaultCredentials();

} // namespace wifi_setup
