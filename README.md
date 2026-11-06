# nuki_matter

ESP32 firmware that bridges a **Nuki Smart Lock** (Bluetooth LE) to **Matter over Wi‑Fi**. It does for Matter what [nuki_hub](https://github.com/technyon/nuki_hub) does for MQTT, and reuses the same Nuki BLE library.

```
Nuki lock  ──BLE──▶  ESP32 (this firmware)  ──Wi‑Fi / Matter──▶  Apple Home · Google Home · Home Assistant · SmartThings
```

The lock appears as a native Matter **Door Lock** with:

| Matter                         | Nuki                                                     |
|--------------------------------|----------------------------------------------------------|
| LockDoor                       | Lock                                                     |
| UnlockDoor                     | Unlock (or Unlatch, if `NUKI_ENABLE_UNBOLT`)             |
| UnboltDoor (optional)          | Unlock                                                   |
| LockState                      | Locked / Unlocked / Unlatched / NotFullyLocked (jammed)  |
| DoorState (optional)           | Door sensor                                              |
| PowerSource (battery %, low)   | Battery level / critical                                 |
| LockAlarm event `LockJammed`   | Motor blocked                                            |

## Hardware

A classic **ESP32** (WROOM/WROVER dev board) with **4 MB flash** is the reference target. Place it within BLE range of the lock.
Other chips aren't supported yet: ESP32‑C3 needs one more warning fix, and ESP32‑S3 pulls in C++20-only code via the Arduino core that conflicts with the Matter SDK.

Resource budget (ESP32, current build): app image 1.85 MB of 1.94 MB OTA slot; IRAM 95 %, static DRAM 84 %.

## Build

Requirements: **ESP-IDF v5.4.x** (developed with v5.4.4). Everything else (esp_matter 1.4.2, arduino-esp32 3.3.12, esp-nimble-cpp, libsodium) comes from the ESP Component Registry on the first build.

```sh
git clone --recursive <this repo>        # or: git submodule update --init --recursive
source ./env.sh                          # activates ESP-IDF (path in env.sh; override with ESPRESSIF_ROOT)
./build.sh esp32 menuconfig              # "Nuki Matter Bridge": Wi-Fi SSID/password, options
./build.sh esp32                         # build
./build.sh esp32 -p /dev/cu.usbserial-XXXX flash monitor
```

`build.sh` keeps a separate build directory and `sdkconfig` per target under `$ESPRESSIF_ROOT/builds/`.

`third_party/nuki_hub` is a submodule pinned to a known-good nuki_hub commit. `components/nuki_ble` compiles that commit's `NukiBleEsp32`, `BleScanner` and `CRC16` libraries as an ESP-IDF component, with the same compile flags nuki_hub uses.

### Build quirks worked around

* arduino-esp32 3.2 and esp_matter 1.4.2 have conflicting dependencies → arduino-esp32 **3.3.12**.
* Only the Arduino `Preferences` library is compiled (`CONFIG_ARDUINO_SELECTIVE_*`).
* arduino-esp32 adds `--wrap` linker flags for esp_matter symbols whose implementations live in its (disabled) Matter library → pass-through shims in `main/arduino_matter_wraps.cpp`.
* The Matter SDK needs C++17 (it breaks under C++20).
* Third-party warnings that are errors under GCC 14 (`-Wformat`, `-Wclass-memaccess` in the Nuki lib, `-Warray-bounds` in esp-nimble-cpp) are relaxed for those components only.
* The ESP RainMaker/Insights components that the Arduino core pulls in run `git describe` and need at least one commit in the repo.

## Setup

1. **Wi‑Fi.** Set the SSID and password in menuconfig, or at runtime on the serial console: `matter esp wifi connect <ssid> <password>`.
2. **Pair with the Nuki.** Hold the lock's button for 5 s until the LED ring lights up. The bridge pairs automatically (log: `paired with xx:xx:...`). It registers as a *bridge*, like nuki_hub does.
   Do not run nuki_hub, or a second bridge, against the same lock at the same time.
3. **Commission into Matter.** On first boot the log prints the setup QR code / manual pairing code. The firmware uses the test credentials (`34970112332` / discriminator 3840) until you add your own DAC, see below. Add the device in your Matter controller. **The phone must be on the same Wi‑Fi**, because commissioning runs on‑network (BLE is used for the Nuki link).

Reset: hold the BOOT button (GPIO 0, configurable) for 10 s. This unpairs from the Nuki and factory-resets Matter. Afterwards, also remove the old bridge authorization in the Nuki app.

## Design notes

* **BLE belongs to Nuki.** Matter's BLE commissioning (`CONFIG_ENABLE_CHIPOBLE`) is off. CHIP's BLE layer and NimBLE‑cpp cannot share the controller cleanly, and the Nuki link has to stay connectable. As a result, controllers that can *only* commission over BLE do not work. Apple Home, Home Assistant (with the phone on the same network) and `chip-tool pairing onnetwork` work.
* **Threads.** Nuki BLE commands block for seconds, so they run in a dedicated `nuki` task. Matter command callbacks only put commands on a queue and return. Status changes are posted back into the CHIP task with `PlatformMgr().ScheduleWork()`.
* **State updates** are event-driven. The Nuki beacon signals a change, and the bridge then reads the key-turner state. A poll every `NUKI_STATE_POLL_INTERVAL_S` (default 10 min) is the safety net. After every command the state is read again, so a failed command doesn't leave the controller stuck at "locking…".
* **Unbolt / unlatch.** Off by default: Matter "Unlock" never opens the door. With `NUKI_ENABLE_UNBOLT` the Matter 1.3+ semantics apply: *Unbolt* retracts the bolt, *Unlock* also pulls the latch.

## Not (yet) implemented

* Nuki Opener (`NukiOpener` is in the same library, so this is a natural next step)
* Keypad codes / users as Matter Users & Credentials
* LockOperation events with the real source (keypad, button, app)
* Security PIN (needed only for some admin commands, not for lock actions)
* Production DAC / certification. Commercial ecosystems show the device as "uncertified" until then.
