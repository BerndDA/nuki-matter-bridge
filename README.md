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

Any ESP32 with Wi‑Fi + BLE and **4 MB+ flash**: ESP32, ESP32‑S3, ESP32‑C3 or ESP32‑C6. Place it within BLE range of the lock.

## Build

Requirements: **ESP-IDF v5.4.1+ (5.4.x)**. All other dependencies (esp_matter 1.4, arduino-esp32 3.2, esp-nimble-cpp, libsodium) come from the ESP Component Registry on the first build.

```sh
git clone --recursive <this repo>        # or: git submodule update --init --recursive
. $IDF_PATH/export.sh
idf.py set-target esp32s3                # or esp32 / esp32c3 / esp32c6
idf.py menuconfig                        # "Nuki Matter Bridge": Wi-Fi SSID/password, options
idf.py build flash monitor
```

`third_party/nuki_hub` is a submodule pinned to a known-good nuki_hub commit. `components/nuki_ble` compiles that commit's `NukiBleEsp32`, `BleScanner` and `CRC16` libraries as an ESP-IDF component, with the same compile flags nuki_hub uses.

## Setup

1. **Wi‑Fi.** Set the SSID and password in menuconfig, or at runtime on the serial console: `matter wifi connect <ssid> <password>`.
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
