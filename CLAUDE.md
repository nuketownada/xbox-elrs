# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-S3 firmware that bridges an Xbox 360 wireless racing wheel (via Microsoft's wireless receiver dongle) to CRSF protocol for ExpressLRS RC transmitters. Written in C using ESP-IDF 5.5.2 with FreeRTOS.

**Hardware target**: Seeed XIAO ESP32-S3 with USB Host (D+/D- pads), CRSF TX on GPIO43, status LED on GPIO21 (active-low), optional UART console on GPIO1/2.

## Build Commands

Requires Nix with flakes. Enter the dev shell first:

```bash
nix develop                          # Enter development shell with ESP-IDF toolchain
idf.py set-target esp32s3            # First time only: set target chip
idf.py menuconfig                    # Configure WiFi credentials (Xbox-ELRS Configuration menu)
idf.py build                         # Build firmware
idf.py -p /dev/ttyACM0 flash         # First flash via USB (hold BOOT, plug USB)
```

Wireless development (after first flash). All commands default to `xbox-elrs.local` via mDNS:

```bash
xbox-log                             # Listen for UDP logs from device (port 3333)
xbox-ota [addr]                      # Build and push OTA firmware update (TCP 3334)
xbox-ping [addr]                     # Check device liveness
xbox-reboot [addr]                   # Remote reboot
```

Fuzz testing (host-side, requires clang):

```bash
nix develop .#fuzz
cmake -B fuzz-build fuzz && cmake --build fuzz-build -j$(nproc)
./fuzz-build/test_disconnect                              # Disconnect notification tests
./fuzz-build/fuzz_parse_report corpus/ -max_total_time=60  # USB report parser
./fuzz-build/fuzz_mixer corpus/ -max_total_time=60         # Channel mixer
./fuzz-build/fuzz_pack_channels corpus/ -max_total_time=60 # CRSF bit packing
```

## Architecture

```
Xbox 360 Wireless Receiver (USB)
        │
  xbox_receiver.c    USB Host driver, vendor-specific protocol parsing
        │                 VID:0x045E PID:0x0719, connect/disconnect/input
        ↓
  xbox_controller_state_t
        │
  channel_mixer.c    Expo curves, deadbands, endpoint limits,
        │                 throttle/brake mixing (COMBINED/SEPARATE/THROTTLE_ONLY),
        ↓                 D-pad steering trim, button mapping, ARM channel
  crsf_channels_t
        │
  crsf.c             UART1 @ 420000 baud, 16ch × 11-bit packed frames, 250Hz
        ↓
  ELRS TX Module
```

Supporting modules:
- **main.c** — Initialization, event routing, status LED task. Xbox callback runs mixer then calls `crsf_set_channels`. Disconnect callback sends disarmed + neutral.
- **wifi.c** — STA mode with persistent reconnection after first success, mDNS (`xbox-elrs.local`), idempotent init on reconnect
- **udp_log.c** — Redirects ESP_LOG to UDP broadcast on port 3333
- **ota.c** — Push-based TCP OTA server on port 3334; protocol: `[4-byte LE size][firmware]`

## Threading Model

- **Xbox Receiver Task** — USB interrupt-driven, calls `xbox_state_callback` which runs mixer and updates CRSF channels
- **CRSF Send Task** — Internal to crsf.c, 250Hz periodic, reads channel state under `s_channels_mutex`
- **LED Task** — Polls WiFi + controller state, blinks GPIO21 accordingly
- **WiFi/mDNS** — Event-driven
- **OTA Server Task** — TCP listener, blocks on accept

Shared state: `s_channels` (crsf_channels_t) inside crsf.c, protected by `s_channels_mutex`. Xbox callback writes via `crsf_set_channels`, CRSF task reads in `send_channels_frame`.

## Safety / Disconnect Handling

When the wireless wheel disconnects (0x08 0x00 packet from receiver), `xbox_receiver.c` fires the user callback with `connected=false`. The callback in `main.c` immediately pushes disarmed + neutral channels via `crsf_set_channels`. The CRSF failsafe (configured at init) also sends disarmed + neutral if the ESP32 itself loses power.

There is no staleness watchdog — the USB receiver reliably sends disconnect notifications when the wheel powers off or goes out of range.

## Key Constants and Ranges

- CRSF channels: 172 (MIN/988µs) → 992 (MID/1500µs) → 1811 (MAX/2012µs)
- Xbox axes: int16 (-32768 to 32767), triggers: uint8 (0-255)
- Expo: 0-100 (0=linear, higher=softer center response)
- Default channel order: AETR (Aileron/Steering, Elevator, Throttle, Rudder/Brake)
- ARM channel: CH5 (AUX1), high when controller connected

## Configuration

Build-time via `idf.py menuconfig` or `sdkconfig.local` (gitignored):
- `CONFIG_WIFI_SSID`, `CONFIG_WIFI_PASSWORD`

Runtime mixer config via `mixer_config_t` / `MIXER_CONFIG_DEFAULT()` in `channel_mixer.h`:
- Steering endpoints: 27/28% (tuned for ~90° wheel rotation to full servo lock)
- Throttle endpoint: 46%, brake endpoint: 28%
- Steering deadband: 3%, throttle deadband: 2%
- D-pad steering trim: ±30% range, 1% steps, resets on power cycle

## References

- [Linux xpad driver](https://github.com/torvalds/linux/blob/master/drivers/input/joystick/xpad.c) — Xbox controller protocol reference
- [CRSF Protocol Wiki](https://github.com/crsf-wg/crsf/wiki) — CRSF specification
- [ESP-IDF USB Host docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/usb_host.html)
