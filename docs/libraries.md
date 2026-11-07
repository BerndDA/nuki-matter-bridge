# Libraries

As of commit a1a6d61 (2026-11-07). The exact versions are in `dependencies.lock`, which is generated
and not committed. The `nuki_hub` code is pinned by its git submodule.

## Linked into the firmware

| Library | Version | Source | License | Used for |
|---|---|---|---|---|
| ESP-IDF | 5.4.4 | Espressif | Apache-2.0 | OS (FreeRTOS), Wi-Fi, lwIP, mbedTLS, NVS, NimBLE host (`bt`) |
| esp_matter (+ connectedhomeip) | 1.4.2 | component registry | Apache-2.0 | Matter stack, Door Lock / Power Source clusters |
| arduino-esp32 | 3.3.12 | component registry | LGPL-2.1 | Only the Arduino core and `Preferences`, which the Nuki library needs |
| esp-nimble-cpp | git `25af28b` | github.com/h2zero/esp-nimble-cpp | Apache-2.0 | C++ BLE client API |
| libsodium | 1.0.22 | component registry | ISC | Nuki session crypto |
| NukiBleEsp32 (`lib/nuki_ble`) | 2.0.0 | nuki_hub submodule | MIT | Nuki BLE protocol: pairing, lock actions, status |
| BleScanner (`lib/BleScanner`) | 1.1.0 | nuki_hub submodule | **GPL-3.0** | BLE scanning, Nuki beacon handling |
| CRC16 (`lib/CRC16`) | 0.1.2 | nuki_hub submodule | MIT | Nuki message checksums |
| esp_diagnostics | 1.3.4 | component registry (via arduino-esp32) | Apache-2.0 | ~2 KB, pulled in by the Arduino core |

The simulation build (`NUKI_SIMULATE=y`) still compiles the BLE/Nuki libraries, but the linker drops them.

`nuki_hub` submodule: `0bfd79a` (2026-11-06). Only the three `lib/` folders above are compiled.
Its other libraries (ArduinoJson, PsychicHttp, espMqttClient, DuoAuthLibrary, …) are unused.

## Downloaded but not linked

arduino-esp32 declares these as dependencies, so the component manager fetches them. Our
code never references them, so they don't end up in the binary:

esp_rainmaker and the rmaker_* components, esp_insights, esp-zigbee-lib / esp-zboss-lib,
esp-modbus, esp_modem, esp-dsp, esp-libhelix-mp3, esp-serial-flasher, esp_delta_ota,
esp_encrypted_img, esp_rcp_update, esp_schedule, esp_daylight, network_provisioning, mdns
(Matter uses its own minimal mDNS), littlefs, lan867x, json_*, jsmn, cbor, qrcode, esp_secure_cert_mgr, esp_diag_data_store.

They only cost download and compile time. The link map confirms that only esp_matter,
esp_diagnostics and arduino-esp32 contribute code from the component registry.

## License note

BleScanner is **GPL-3.0**, so a firmware binary that includes it may only be distributed under
GPL-3.0 terms, with the source made available. This doesn't matter for private use. It does matter
if the firmware is ever shared or published.
