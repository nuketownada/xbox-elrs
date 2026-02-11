# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-S3 firmware that bridges an Xbox 360 wireless racing wheel (via Microsoft's wireless receiver dongle) to CRSF protocol for ExpressLRS RC transmitters. Written in C using ESP-IDF 5.5.2 with FreeRTOS.

**Hardware target**: Seeed XIAO ESP32-S3 with USB Host (D+/D- pads), CRSF TX on GPIO43, optional UART console on GPIO1/2.

## Build Commands

Requires Nix with flakes. Enter the dev shell first:

```bash
nix develop                          # Enter development shell with ESP-IDF toolchain
idf.py set-target esp32s3            # First time only: set target chip
idf.py menuconfig                    # Configure WiFi credentials (Xbox-ELRS Configuration menu)
idf.py build                         # Build firmware
idf.py -p /dev/ttyACM0 flash         # First flash via USB (hold BOOT, tap RESET, release BOOT)
```

Wireless development (after first flash):

```bash
xbox-log                             # Listen for UDP logs from device (port 3333)
xbox-ota [ip|xbox-elrs.local]        # Build and push OTA firmware update
xbox-ping [ip|xbox-elrs.local]       # Check device liveness
xbox-reboot [ip|xbox-elrs.local]     # Remote reboot
```

No test framework or linter is configured.

## Architecture

```
Xbox 360 Wireless Receiver (USB)
        │
  xbox_receiver.c    USB Host driver, vendor-specific protocol parsing
        │                 VID:0x045E PID:0x0719, button/axis decoding
        ↓
  xbox_controller_state_t
        │
  channel_mixer.c    Expo curves, deadbands, endpoint limits,
        │                 throttle/brake mixing (COMBINED/SEPARATE/THROTTLE_ONLY),
        ↓                 button-to-channel mapping, ARM channel
  crsf_channels_t
        │
  crsf.c             UART1 @ 420000 baud, 16ch × 11-bit packed frames
        ↓
  ELRS TX Module
```

Supporting modules:
- **main.c** — Initialization and event routing. Xbox callback runs mixer then calls `crsf_set_channels`.
- **wifi.c** — STA mode with retry (persistent reconnection after first success), mDNS (`xbox-elrs.local`)
- **udp_log.c** — Redirects ESP_LOG to UDP broadcast on port 3333
- **ota.c** — Push-based TCP OTA server on port 3334; protocol: `[4-byte LE size][firmware]`

## Threading Model

- **Xbox Receiver Task** — USB interrupt-driven, calls `xbox_state_callback` which runs mixer and updates CRSF
- **CRSF Send Task** — Internal to crsf.c, 250Hz periodic, reads channel state under `s_channels_mutex`
- **WiFi/mDNS** — Event-driven
- **OTA Server Task** — TCP listener

Shared state: `s_channels` (crsf_channels_t) inside crsf.c, protected by `s_channels_mutex`. Xbox callback writes via `crsf_set_channels`, CRSF task reads in `send_channels_frame`.

## Key Constants and Ranges

- CRSF channels: 172 (MIN/988µs) → 992 (MID/1500µs) → 1811 (MAX/2012µs)
- Xbox axes: int16 (-32768 to 32767), triggers: uint8 (0-255)
- Expo: 0-100 (0=linear, higher=softer center response)
- Default channel order: AETR (Aileron/Steering, Elevator, Throttle, Rudder/Brake)

## Configuration

Build-time via `idf.py menuconfig` or `sdkconfig.local`:
- `CONFIG_WIFI_SSID`, `CONFIG_WIFI_PASSWORD`, `CONFIG_UDP_LOG_HOST`

Runtime mixer config via `mixer_config_t` / `MIXER_CONFIG_DEFAULT()` in `channel_mixer.h`. Endpoint percentages are currently tuned for a specific wheel — adjust `steering_endpoint_left/right`, `throttle_endpoint`, `brake_endpoint` for your hardware.

## References

- [Linux xpad driver](https://github.com/torvalds/linux/blob/master/drivers/input/joystick/xpad.c) — Xbox controller protocol reference
- [CRSF Protocol Wiki](https://github.com/crsf-wg/crsf/wiki) — CRSF specification
- [ESP-IDF USB Host docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/usb_host.html)
