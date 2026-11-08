# nuki_matter

ESP32 firmware that bridges a **Nuki Smart Lock** (Bluetooth LE) to **Matter over Wi‑Fi**, so the lock works natively in Apple Home, Google Home, Home Assistant and other Matter controllers. It is inspired by [nuki_hub](https://github.com/technyon/nuki_hub), which does the same for MQTT / Home Assistant, and reuses nuki_hub's Nuki BLE libraries.

```
Nuki lock  ──BLE──▶  ESP32 (this firmware)  ──Wi‑Fi / Matter──▶  Apple Home · Google Home · Home Assistant · …
```

> Hobby project, not affiliated with Nuki. It runs with Matter **test credentials**, so controllers show it as an uncertified device.

## Features

- **Matter bridge with two door locks**:
  - **Front door**: lock and unlock (the bolt only, the door stays shut).
  - **Door opener**: unlocking it pulls the latch (opens the door) and then shows Locked again. Apple Home has no separate "unlatch" button, and it treats this as a lock, so Face ID or a passcode is still required. See [docs/open-door-control.md](docs/open-door-control.md).
- Lock state, battery level and a "jammed" alarm. Door sensor state is optional.
- The accessory info shown in controllers comes from the lock: Nuki ID as serial number, plus firmware and hardware version.
- **Web page** on port 80 with:
  - status of the lock, Matter pairings, Wi‑Fi and memory/task diagnostics
  - maintenance actions: restart, open the pairing window, unpair the Nuki, Matter factory reset
  - a live log
- **Firmware updates over Wi‑Fi** (`make ota`). They are password protected, and a broken update is rolled back automatically.
- **Network log** (`make log` / `nc <bridge> 23`), kept across restarts and crashes, so you can debug a bridge mounted at the door.

## Hardware

- A classic **ESP32** dev board (WROOM/WROVER) with **4 MB flash**, placed within BLE range of the lock.
- Tested with a Nuki Smart Lock (hardware 8.3, firmware 4.10.5).
- **ESP32-C3** (4 MB flash) works too: build with `make TARGET=esp32c3`. Matter, the web UI and OTA are tested; pairing with a lock on the C3 isn't yet. C3-specific memory settings are in `sdkconfig.defaults.esp32c3`.
- Other ESP32 variants aren't supported yet.

## Build

Requirements:
- **ESP-IDF v5.4.x** (developed with v5.4.4).
- Everything else is fetched from the ESP Component Registry on the first build: esp_matter 1.4.2, arduino-esp32 3.3.12, esp-nimble-cpp, libsodium.

```sh
git clone --recursive https://github.com/BerndDA/nuki-matter-bridge.git
cd nuki-matter-bridge
cp config.local.example config.local   # ESP-IDF path, serial PORT, bridge HOST (all optional)
make menuconfig                        # "Nuki Matter Bridge": Wi-Fi, names, OTA password, ...
make run                               # build, flash over USB, open the serial console
```

| Command | What it does |
|---|---|
| `make` | Build into `build/esp32/` (`nuki_matter.bin`, `.elf`, `sdkconfig`, …) |
| `make flash` / `make monitor` / `make run` | Flash over USB / serial console / both |
| `make ota` | Build and update over Wi‑Fi, then verify the bridge runs the new image |
| `make log` | Live log over Wi‑Fi; quit with Ctrl+C |
| `make menuconfig` / `make size` / `make erase` | Options / size report / wipe the whole flash |

To leave the serial console, press **Ctrl + ]**, or **Ctrl + T** then **Ctrl + X**. On a German Mac keyboard, Ctrl + ] is Ctrl + Option + 9.

If you set `BUILD_STORAGE` in `config.local`, `build/<target>` becomes a symlink into that directory, for example on an external drive.

## Setup

1. **Wi‑Fi.** Set it in menuconfig, or at runtime on the serial console: `matter esp wifi connect <ssid> <password>`.
2. **Pair with the Nuki.** Hold the lock's button for 5 s. The bridge pairs automatically and registers as a *bridge*, like nuki_hub does. Don't run nuki_hub or another bridge against the same lock at the same time.
3. **Add the bridge to your Matter controller.**
   - The setup code is `3497-011-2332` (Matter test credentials). The QR code is in the boot log and on the web page.
   - Commissioning runs **on-network**, because BLE is reserved for the Nuki link. Your phone must be on the same Wi‑Fi.
4. **OTA updates (optional).** Set an OTA password in menuconfig and flash once over USB. After that, `make ota` is enough.

**Reset:** hold the BOOT button (GPIO 0) for 10 s. This unpairs the Nuki and factory-resets Matter. Then remove the old bridge authorization in the Nuki app.

## Design notes

- **BLE belongs to the Nuki.** Matter's BLE commissioning is disabled. CHIP's BLE layer and NimBLE‑cpp can't share the controller, and the Nuki link has to stay connectable.
- **Threads.** Nuki BLE commands block for seconds, so they run in their own task. Matter callbacks only put commands on a queue. Status changes are posted back to the CHIP task with `PlatformMgr().ScheduleWork()`.
- **State updates are event-driven.** The Nuki's beacon signals a change, and the bridge then reads the lock state. A poll every 10 minutes is the safety net.
- **Memory.** The classic ESP32 is tight with Matter, Wi‑Fi and BLE together. [docs/flash-budget.md](docs/flash-budget.md) covers flash and the trims applied. The web page shows heap and per-task stack headroom.
- **Wi‑Fi** always connects to the strongest AP. With a mesh or repeaters, the default "first AP found" could leave the bridge without DHCP.

More in [docs/](docs/):
- [open-door-control.md](docs/open-door-control.md): unlatching and Apple Home
- [flash-budget.md](docs/flash-budget.md): flash budget and trims
- [libraries.md](docs/libraries.md): library versions and licenses

## Not (yet) implemented

- Nuki Opener (`NukiOpener` is in the same library)
- Keypad codes / users as Matter Users & Credentials
- LockOperation events with the real source (keypad, button, app)
- Production DAC / certification

## Credits

This project builds on the work of others:

- **[nuki_hub](https://github.com/technyon/nuki_hub)** by Jan-Ole Schümann (technyon) and contributors (MIT). It inspired this project, and its Nuki libraries are used through the `third_party/nuki_hub` submodule. Many design choices and settings follow nuki_hub.
- **[NukiBleEsp32](https://github.com/I-Connect/NukiBleEsp32)** by I-Connect (MIT): the Nuki BLE protocol (pairing, lock actions, status).
- **[BleScanner](https://github.com/I-Connect/BleScanner)** by I-Connect (**GPL-3.0**): BLE scanning and Nuki beacon handling.
- **[Crc16](https://github.com/vinmenn/Crc16)** by Vincenzo Mennella (MIT).
- **[esp-nimble-cpp](https://github.com/h2zero/esp-nimble-cpp)** by h2zero (Apache-2.0): C++ BLE client API.
- **[esp-matter](https://github.com/espressif/esp-matter)** by Espressif and **[connectedhomeip](https://github.com/project-chip/connectedhomeip)** by Project CHIP / Connectivity Standards Alliance (Apache-2.0): the Matter stack.
- **[ESP-IDF](https://github.com/espressif/esp-idf)** (Apache-2.0) and **[arduino-esp32](https://github.com/espressif/arduino-esp32)** (LGPL-2.1) by Espressif.
- **[libsodium](https://github.com/jedisct1/libsodium)** by Frank Denis (ISC): Nuki session crypto.
- **[home-assistant-matter-hub](https://github.com/RiDDiX/home-assistant-matter-hub)** by RiDDiX: its findings on how Apple Home handles Matter unlatch shaped the "Door opener" design.

## License

See [LICENSE](LICENSE). Note that firmware binaries built from this repository include BleScanner (GPL-3.0), so distributed binaries fall under the GPL-3.0. [docs/libraries.md](docs/libraries.md) has the full list.
