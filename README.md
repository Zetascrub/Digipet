# Digipet

Digipet is an ESP32-S3 virtual pet firmware for the Waveshare ESP32-S3 Touch
AMOLED 1.8-inch V2 board (CO5300 display and CST820 touch controller).

The project blends Digivice-inspired presentation with a modern AMOLED touch
interface. It is an original virtual-pet project and does not include Digimon
artwork or proprietary assets.

## Current features

- Five-second artifact-convergence boot sequence with audio
- Smooth PSRAM-backed page transitions and full-frame rendering
- Animated companion, status, battle, and settings pages
- Touch gestures, idle dimming, display-off, and configurable wake control
- QMI8658 lift-to-wake support
- RTC clock with optional SD-card Wi-Fi provisioning and NTP synchronisation
- ES8311 speaker output with configurable volume
- Hardware diagnostics for the IMU, RTC, power controller, and audio codec
- Cross-device BLE battles compatible with the included VPet Battle protocol
- Runtime SHA-256 player identity generation
- Persistent settings and virtual-pet state through ESP32 Preferences/NVS

Poop and illness mechanics are intentionally excluded.

## Hardware

This firmware targets the V2 board:

```text
Waveshare ESP32-S3-Touch-AMOLED-1.8
Display: CO5300, 368 x 448
Touch:   CST820/CST816-compatible controller
MCU:     ESP32-S3 with 8 MB PSRAM and 16 MB flash
```

It is not intended for the visually similar V1 display variant.

## Build and flash

Install [PlatformIO](https://platformio.org/), connect the board, then run:

```bash
pio run
pio run -t upload --upload-port /dev/ttyACM0
pio device monitor --port /dev/ttyACM0 --baud 115200
```

Change the serial device where required for your operating system.

The first build downloads NimBLE-Arduino. Board-specific display, touch, and
bus libraries are vendored under `lib/` because this board requires Waveshare
drivers that are not fully represented by the generic PlatformIO board target.

## Optional Wi-Fi and time setup

Copy [`examples/wifi.ini.example`](examples/wifi.ini.example) to
`/digipet/wifi.ini` on a microSD card and replace the placeholders locally.

The device reads the file during boot, connects long enough to synchronise its
clock, then disables Wi-Fi. Failure to mount the card, connect, or sync does not
prevent normal offline operation.

Never commit a populated `wifi.ini`. The `provisioning/` directory is ignored
for developers who want a local staging area for private device files.

## Cross-device battle package

The live firmware uses `include/familiar_battle_service.h` and
`src/familiar_battle_service.cpp`. A self-contained handoff package for other
ESP32 developers is available under [`share/vpet-battle`](share/vpet-battle),
including integration instructions and a compatible Python BLE simulator.

Battle rendering is device-specific. The shared service owns discovery,
handshaking, moves, deterministic turn resolution, stats, outcomes, and the BLE
wire protocol.

## Repository layout

```text
include/             Public firmware headers
src/                 Digipet firmware and battle service
lib/                 Vendored Waveshare-compatible device libraries
examples/            Credential-free configuration examples
share/vpet-battle/   Reusable battle service handoff package
platformio.ini       PlatformIO build configuration
```

## License

Digipet is released under the MIT License. Vendored libraries retain their own
licence files and copyright notices.
