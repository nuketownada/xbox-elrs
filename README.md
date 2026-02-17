# Xbox 360 Racing Wheel to ELRS Transmitter Bridge

Converts input from an Xbox 360 wireless racing wheel (via Microsoft's wireless receiver dongle) to CRSF protocol for ExpressLRS RC transmitters. Designed for driving RC cars with a full-size racing wheel.

## Hardware

- **MCU**: Seeed XIAO ESP32-S3 (or similar ESP32-S3 board)
- **Input**: Microsoft Xbox 360 Wireless Receiver for Windows
- **Output**: Any ELRS TX module running TX firmware
- **Controller**: Xbox 360 wireless racing wheel with force feedback

### Wiring

```
XIAO ESP32-S3
┌─────────────────┐
│                 │
│  USB-C ─────────┼──── Computer (first flash only, via boot mode)
│                 │
│  D+/D- pads ────┼──── USB-A female ──── Xbox 360 Receiver
│                 │
│  GPIO43 (D6) ───┼──── ELRS TX "S" or "CRSF" pin
│                 │
│  GPIO1/2 (D0/D1)┼──── USB-UART adapter (optional, for UART console)
│                 │
│  5V ────────────┼──┬─ Xbox Receiver 5V
│                 │  └─ ELRS TX 5V
│                 │
│  GND ───────────┼──┬─ Xbox Receiver GND
│                 │  └─ ELRS TX GND
└─────────────────┘
```

**USB Host Wiring Detail**: The XIAO ESP32-S3 exposes D+/D- pads on the bottom of the board. Solder a USB-A female connector to these pads:
- D+ pad → USB green wire
- D- pad → USB white wire
- 5V pin → USB red wire
- GND → USB black wire

Add a 10-47µF capacitor between 5V and GND near the USB connector for stable enumeration.

**Note**: The USB-C port switches to host mode at runtime, so it can't be used for serial console after the firmware starts. Use the optional USB-UART adapter on D0/D1 if you need a physical serial console.

## Building

### Prerequisites

- Nix with flakes enabled

### Development Shell

```bash
nix develop
```

### Configure WiFi

Before first build, set your WiFi credentials. Create `sdkconfig.local` (gitignored):

```
CONFIG_WIFI_SSID="your_ssid"
CONFIG_WIFI_PASSWORD="your_password"
```

Or use menuconfig:

```bash
idf.py menuconfig
# Navigate to: Xbox-ELRS Configuration
# Set WiFi SSID and Password
```

WiFi is optional — the device works without it, but you lose OTA updates, UDP logging, and mDNS.

### Build

```bash
# First time: set target chip
idf.py set-target esp32s3

# Build firmware
idf.py build
```

### First Flash (USB Required)

The first flash must be done via USB since OTA isn't available yet:

```bash
# Put device in boot mode: Hold BOOT, plug in USB (or hold BOOT, tap RESET, release BOOT)
idf.py -p /dev/ttyACM0 flash
```

**Tip**: The USB-JTAG serial port disappears once firmware starts (USB switches to host mode). If the port vanishes before esptool connects, use `--before no_reset` and enter boot mode manually.

### Subsequent Updates (OTA)

After the first flash, use wireless OTA. The device advertises itself as `xbox-elrs.local` via mDNS:

```bash
# Build and push firmware
xbox-ota                        # Uses xbox-elrs.local by default
xbox-ota 192.168.1.42           # Or specify IP directly
```

## Wireless Development

The dev shell provides helper commands. All default to `xbox-elrs.local` if no address is given:

| Command | Description |
|---------|-------------|
| `xbox-log` | Listen for UDP log broadcasts from device (port 3333) |
| `xbox-ota [addr]` | Build and push OTA firmware update (TCP port 3334) |
| `xbox-ping [addr]` | Check if device is responding |
| `xbox-reboot [addr]` | Remotely reboot device |

### Network Services

| Service | Port | Protocol | Description |
|---------|------|----------|-------------|
| UDP logging | 3333 | UDP broadcast | ESP_LOG output, receive with `xbox-log` or `socat -u UDP-LISTEN:3333,fork STDOUT` |
| OTA server | 3334 | TCP | Push-based firmware update: `[4-byte LE size][firmware bytes]` |
| mDNS | 5353 | UDP | Hostname `xbox-elrs.local` |

## Status LED

The onboard LED (GPIO21, active-low) indicates system state:

| Pattern | Meaning |
|---------|---------|
| Slow blink (1Hz) | All good — WiFi connected, controller paired |
| Fast blink (4Hz) | No WiFi connection |
| Solid on | WiFi ok, waiting for controller |

## Channel Mapping

Default mapping follows AETR convention:

| Channel | Function | Xbox Input |
|---------|----------|------------|
| CH1 (Aileron) | Steering | Wheel rotation |
| CH2 (Elevator) | Unused | — |
| CH3 (Throttle) | Throttle/Brake | Right/Left Triggers (combined) |
| CH4 (Rudder) | Brake (separate mode only) | Left Trigger |
| CH5 (AUX1) | ARM | Auto-set high when controller connected |
| CH6 (AUX2) | Paddle Left | LB or A button |
| CH7 (AUX3) | Paddle Right | RB or B button |
| CH8 (AUX4) | Button A | A |
| CH9 (AUX5) | Button B | B |
| CH10 (AUX6) | Button X | X |
| CH11 (AUX7) | Button Y | Y |

### Steering Trim

D-pad adjusts steering trim at runtime (resets on power cycle):

- **D-pad Left/Right**: Adjust trim in 1% steps (up to ±30%)
- **D-pad Up**: Reset trim to center

### Throttle Modes

- **Combined** (default): Single channel, center=stop, forward=throttle, reverse=brake. Standard for most RC car ESCs.
- **Separate**: Throttle on CH3, Brake on CH4.
- **Throttle Only**: Ignore brake input.

### Endpoint Tuning

Endpoints scale the output range for each axis. Adjust in `channel_mixer.h` via `MIXER_CONFIG_DEFAULT()`:

| Parameter | Default | Effect |
|-----------|---------|--------|
| `steering_endpoint_left` | 27% | Left steering throw |
| `steering_endpoint_right` | 28% | Right steering throw |
| `throttle_endpoint` | 46% | Forward throttle range |
| `brake_endpoint` | 28% | Reverse/brake range |

Lower values = more wheel/trigger travel needed for full servo deflection. The steering defaults are tuned for ~90 degrees of wheel rotation to full servo lock.

## Safety

### Disconnect Handling

When the racing wheel loses its wireless link to the USB receiver (powers off, goes out of range), the receiver sends a disconnect notification. The firmware immediately:

1. Sets all channels to neutral
2. Sets the ARM channel (CH5) to MIN (disarmed)
3. The flight controller (e.g. GroundFlight) sees the disarm and engages its own failsafe (ebrake, motor cutoff)

The same safe state is applied as the CRSF failsafe — if the ESP32 itself loses power, the ELRS TX module's built-in failsafe sends disarmed + neutral.

### Boot Sequence

On startup, throttle is held at minimum and ARM is disarmed until the wheel connects and sends its first input. The car won't move until you actively pull a trigger.

## Fuzz Testing

A libFuzzer harness tests the input-handling code paths on the host:

```bash
nix develop .#fuzz
cmake -B fuzz-build fuzz && cmake --build fuzz-build -j$(nproc)

# Run tests
./fuzz-build/test_disconnect                              # Disconnect notification tests
./fuzz-build/fuzz_parse_report corpus/ -max_total_time=60  # USB report parser
./fuzz-build/fuzz_mixer corpus/ -max_total_time=60         # Channel mixer
./fuzz-build/fuzz_pack_channels corpus/ -max_total_time=60 # CRSF bit packing
```

## Troubleshooting

### Device not appearing after flash

The USB port switches to host mode, so it won't enumerate as a serial device. This is expected. Use:
- `xbox-log` for wireless logs (once WiFi connects)
- USB-UART adapter on D0/D1 for physical serial console
- Boot mode (hold BOOT + plug USB) to re-flash

### WiFi not connecting

- Check credentials in `sdkconfig.local` or menuconfig
- Device continues without network features if WiFi fails
- Look for "WiFi connection failed" in early boot logs (via UART)

### OTA fails

- Ensure device and computer are on the same network
- Check firewall allows UDP 3333 and TCP 3334
- Verify firmware file exists: `build/xbox-elrs.bin`
- Try IP address directly if mDNS isn't resolving: `xbox-ota 192.168.x.x`

### LED fluttering at high frequency

Device is in a bad state. Unplug and replug — the reset button alone may not recover it.

## Architecture

```
Xbox 360 Wireless Receiver (USB)
        │
  xbox_receiver.c ── USB Host driver, vendor-specific protocol
        │               VID:045E PID:0719, connect/disconnect/input parsing
        ↓
  xbox_controller_state_t
        │
  channel_mixer.c ── Expo curves, deadbands, endpoint limits,
        │               throttle/brake mixing, D-pad trim, button mapping
        ↓
  crsf_channels_t
        │
  crsf.c ─────────── UART1 @ 420000 baud, 16ch × 11-bit packed, 250Hz
        ↓
  ELRS TX Module
```

Supporting modules:
- **wifi.c** — STA mode with persistent reconnection, mDNS (`xbox-elrs.local`)
- **udp_log.c** — Redirects ESP_LOG to UDP broadcast on port 3333
- **ota.c** — Push-based TCP OTA server on port 3334

## References

- [Linux xpad driver](https://github.com/torvalds/linux/blob/master/drivers/input/joystick/xpad.c) — Xbox controller protocol reference
- [CRSF Protocol Wiki](https://github.com/crsf-wg/crsf/wiki) — CRSF specification
- [ESP-IDF USB Host](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/usb_host.html) — ESP32-S3 USB Host documentation
- [nixpkgs-esp-dev](https://github.com/mirrexagon/nixpkgs-esp-dev) — Nix flake for ESP-IDF toolchain

## License

MIT
