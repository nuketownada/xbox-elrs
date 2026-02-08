# Xbox 360 Racing Wheel to ELRS Transmitter Bridge

Converts input from an Xbox 360 wireless racing wheel (via Microsoft's wireless receiver dongle) to CRSF protocol for ExpressLRS RC transmitters.

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

Before first build, set your WiFi credentials:

```bash
idf.py menuconfig
# Navigate to: Xbox-ELRS Configuration
# Set WiFi SSID and Password
```

Or create `sdkconfig.local`:
```
CONFIG_WIFI_SSID="your_actual_ssid"
CONFIG_WIFI_PASSWORD="your_actual_password"
```

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
# Put device in boot mode: Hold BOOT, tap RESET, release BOOT
idf.py -p /dev/ttyACM0 flash
```

### Subsequent Updates (OTA)

After the first flash, use wireless OTA:

```bash
# In one terminal - watch device logs
xbox-log

# In another terminal - trigger OTA
xbox-ota 192.168.1.42    # Replace with your device's IP
```

The device IP is printed to the log on boot after WiFi connects.

## Wireless Development

The dev shell provides helper commands for wireless development:

| Command | Description |
|---------|-------------|
| `xbox-log` | Receive UDP logs from device (port 3333) |
| `xbox-ota <ip>` | Build and OTA flash to device |
| `xbox-ping <ip>` | Check if device is responding |
| `xbox-reboot <ip>` | Remotely reboot device |

### How It Works

- **UDP Logging**: Device broadcasts log messages to port 3333. Use `xbox-log` or `nc -u -l -p 3333` to receive.
- **OTA Updates**: Device listens on UDP port 3334 for commands. Send `OTA http://host:port/firmware.bin` to trigger an update.

## Configuration

### Channel Mapping

Default mapping follows AETR convention:

| Channel | Function | Xbox Input |
|---------|----------|------------|
| CH1 (Aileron) | Steering | Left Stick X / Wheel rotation |
| CH2 (Elevator) | Unused | - |
| CH3 (Throttle) | Throttle/Brake | Right/Left Triggers (combined) |
| CH4 (Rudder) | Brake (separate mode) | Left Trigger |
| CH5 (AUX1) | Paddle Left | LB or A |
| CH6 (AUX2) | Paddle Right | RB or B |
| CH7-CH16 | Additional buttons | Configurable |

### Throttle Modes

- **Combined** (default): Single channel, center=stop, forward=throttle, reverse=brake. Standard for most RC car ESCs.
- **Separate**: Throttle on CH3, Brake on CH4. For crawlers or boats with separate ESC channels.
- **Throttle Only**: Ignore brake input.

## Status

**Work in Progress**

- [x] Project structure and build system
- [x] CRSF protocol implementation
- [x] Channel mixer with expo/deadband
- [x] WiFi connectivity
- [x] UDP wireless logging
- [x] OTA updates
- [ ] USB Host enumeration and device detection
- [ ] Xbox receiver protocol parsing (structure in place, needs testing)
- [ ] Force feedback output (rumble)
- [ ] Runtime configuration
- [ ] Failsafe handling

## Troubleshooting

### Device not appearing after flash

The USB port switches to host mode, so it won't enumerate as a serial device. This is expected. Use:
- `xbox-log` for wireless logs
- USB-UART adapter on D0/D1 for physical serial console
- Boot mode (hold BOOT + tap RESET) to re-flash via USB

### WiFi not connecting

- Check credentials in menuconfig
- Device will continue without network features if WiFi fails
- Look for "WiFi connection failed" in early boot logs (via UART)

### OTA fails

- Ensure device and computer are on same network
- Check firewall allows UDP 3333/3334 and TCP 8080
- Verify firmware file exists: `build/xbox-elrs.bin`

## References

- [Linux xpad driver](https://github.com/torvalds/linux/blob/master/drivers/input/joystick/xpad.c) - Xbox controller protocol reference
- [CRSF Protocol Wiki](https://github.com/crsf-wg/crsf/wiki) - CRSF specification
- [ESP-IDF USB Host](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/usb_host.html) - ESP32-S3 USB Host documentation
- [nixpkgs-esp-dev](https://github.com/mirrexagon/nixpkgs-esp-dev) - Nix flake for ESP-IDF toolchain

## License

MIT
