# Flash budget (classic ESP32, 4 MB)

Measured 2026-11-07 with ESP-IDF 5.4.4 and esp_matter 1.4.2, at `-Os`.

## Partitions

Each OTA slot (`ota_0`, `ota_1`) is **0x1F0000 = 2,031,616 bytes (1.94 MB)**. The app has to fit
in one slot. See [partitions.csv](../partitions.csv).

## App size per slot

| Build | Before trims | After trims (a1a6d61) |
|---|---|---|
| Simulation (`NUKI_SIMULATE=y`, no BLE) | 1,647,762 B, 19% free | 1,218,208 B (0x1296A0), **40% free** (813 KB) |
| Real (Nuki over BLE) | ~1.95 MB, **~4% free** | 1,484,768 B (0x16A7E0), **27% free** (547 KB) |

Static RAM in the simulation build went from DRAM 98,672 B to 86,008 B (−12.6 KB). IRAM stayed
at 76.5%.

## What uses the space (simulation build, before trims)

| Part | Size | Notes |
|---|---|---|
| Matter SDK (`libespressif__esp_matter.a`) | 859 KB | 460 KB code, 341 KB const data |
| C++ exception tables (`.eh_frame`) | 309 KB | `idf.py size` shows it under `libesp_app_format.a` |
| Wi-Fi (net80211, pp, phy, wpa_supplicant) | ~285 KB | fixed cost |
| lwIP | 96 KB | |
| mbedTLS (crypto) | 77 KB | |
| newlib nano, FreeRTOS, NVS, rest | ~100 KB | |

The real build adds NimBLE, the Nuki library, libsodium and the Arduino core (Preferences only):
about 265 KB after the trims.

## Trims applied (sdkconfig.defaults)

| Change | Saved |
|---|---|
| `CONFIG_COMPILER_CXX_EXCEPTIONS=n`: no code uses exceptions | ~309 KB |
| Only 23 of 123 Matter clusters built (`CONFIG_SUPPORT_*_CLUSTER=n`) | ~120 KB flash, ~12 KB DRAM |

Kept clusters: AccessControl, AdministratorCommissioning, BasicInformation, Binding (required by
`esp_matter_client.cpp`), Descriptor, DiagnosticLogs, DoorLock, FixedLabel, GeneralCommissioning,
GeneralDiagnostics, GroupKeyManagement, IcdManagement, Identify, LocalizationConfiguration,
NetworkCommissioning, OperationalCredentials, OTA Provider/Requestor, PowerSource,
SoftwareDiagnostics, TimeFormatLocalization, UserLabel, WiFiNetworkDiagnostics.

Before adding a feature that needs another cluster (e.g. OnOff for an "open door" switch),
re-enable that `CONFIG_SUPPORT_<NAME>_CLUSTER`.

## Further options (not applied)

| Option | Estimated saving | Downside |
|---|---|---|
| Matter log level Error only (`CHIP_LOG_DEFAULT_LEVEL_ERROR`) | tens of KB | loses the progress log used for debugging |
| Drop the HTTPS certificate bundle (`MBEDTLS_CERTIFICATE_BUNDLE=n`) | ~60 KB at most | no HTTPS to public servers (not used today) |
| Drop ESP Insights / diagnostics | a few KB | none currently |
| Drop the CHIP shell | ~10–20 KB | no `matter esp wifi connect` on the console |

## Re-measuring

```sh
make size                                         # summary
source ./env.sh && ./build.sh esp32 size-components   # per library
```
