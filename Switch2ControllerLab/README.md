# Switch2 Controller Lab Native Helper

This folder contains native macOS C tools for the Nintendo Switch 2 Pro
Controller over USB.

The main tool is `switch2_mac_bridge`. It initializes the controller over USB,
reads HID input reports, decodes controller state, prints live input, and can
stream normalized packets to localhost for game engines such as Unity.

This project is not affiliated with Nintendo.

## Files

```text
switch2_hid_probe.c      Minimal HID enumeration/report probe.
switch2_mac_bridge.c     USB initialization + HID decoding + UDP bridge.
```

## What The Helper Does

The Switch 2 Pro Controller is visible to macOS as a USB HID gamepad, but it
does not immediately stream useful input reports. The helper performs a USB
bulk initialization sequence on interface `1`, then listens for HID input
reports.

Known device identity:

- Vendor ID: `0x057e`
- Product ID: `0x2069`
- Product name: `Switch 2 Pro Controller`
- Transport: `USB`
- HID usage page: `0x01`
- HID usage: `0x05` / gamepad

Known USB endpoints after enumeration:

- Interface: `1`
- Bulk OUT endpoint: `0x02`
- Bulk IN endpoint: `0x82`

Input reports:

- Report ID: `9`
- Length: 64 bytes
- Stick format: two 12-bit values packed into each three-byte stick block

```c
x = data[0] | ((data[1] & 0x0f) << 8);
y = (data[1] >> 4) | (data[2] << 4);
```

## Requirements

- macOS
- Clang
- Homebrew
- `libusb-1.0`
- USB-connected Switch 2 Pro Controller

Install dependencies:

```sh
brew install libusb pkg-config
```

## Build

From the repository root:

```sh
clang -Wall -Wextra -Wpedantic -std=c11 \
  Switch2ControllerLab/switch2_mac_bridge.c \
  $(pkg-config --cflags --libs libusb-1.0) \
  -framework IOKit -framework CoreFoundation \
  -o Switch2ControllerLab/switch2_mac_bridge
```

If `libusb` is installed through Intel Homebrew under `/usr/local` on an Apple
Silicon Mac, build as an `x86_64` Rosetta binary:

```sh
clang -arch x86_64 -Wall -Wextra -Wpedantic -std=c11 \
  Switch2ControllerLab/switch2_mac_bridge.c \
  -I/usr/local/Cellar/libusb/1.0.29/include/libusb-1.0 \
  -L/usr/local/Cellar/libusb/1.0.29/lib -lusb-1.0 \
  -framework IOKit -framework CoreFoundation \
  -o Switch2ControllerLab/switch2_mac_bridge
```

Build the smaller HID probe:

```sh
clang -Wall -Wextra -Wpedantic -std=c11 \
  Switch2ControllerLab/switch2_hid_probe.c \
  -framework IOKit -framework CoreFoundation \
  -o Switch2ControllerLab/switch2_hid_probe
```

## Usage

Print help:

```sh
Switch2ControllerLab/switch2_mac_bridge --help
```

Initialize the controller and print meaningful input changes:

```sh
Switch2ControllerLab/switch2_mac_bridge --seconds 10
```

Stream controller state to the Unity bridge:

```sh
Switch2ControllerLab/switch2_mac_bridge --udp 127.0.0.1:28782
```

Run a short Unity bridge smoke test:

```sh
Switch2ControllerLab/switch2_mac_bridge --udp 127.0.0.1:28782 --seconds 15
```

Print only button/trigger changes:

```sh
Switch2ControllerLab/switch2_mac_bridge --buttons-only --seconds 10
```

Only send the USB initialization sequence:

```sh
Switch2ControllerLab/switch2_mac_bridge --init-only
```

Only listen to HID reports after another process has initialized the controller:

```sh
Switch2ControllerLab/switch2_mac_bridge --listen-only
```

USB/HID access may require appropriate local permissions. If direct execution
fails with a permissions error, try running from a normal Terminal session with
elevated privileges.

## UDP Output

Use `--udp HOST:PORT` to send one JSON object per meaningful input state change.
The Unity package defaults to port `28782`.

Example:

```json
{"version":1,"sequence":1,"timestamp":769167220.123456,"vendorId":1406,"productId":8297,"buttonMask":34,"buttons0":34,"buttons1":0,"buttons2":0,"leftStickX":0.01,"leftStickY":0.08,"rightStickX":0.09,"rightStickY":0.0,"leftTrigger":0.0,"rightTrigger":1.0}
```

## Confirmed Button Bits

Known button bits from hardware captures:

| Button | Byte | Mask |
| --- | ---: | ---: |
| B | `0` | `0x01` |
| A | `0` | `0x02` |
| Y | `0` | `0x04` |
| X | `0` | `0x08` |
| R | `0` | `0x10` |
| ZR | `0` | `0x20` |
| Plus | `0` | `0x40` |
| Right stick click | `0` | `0x80` |
| D-pad down | `1` | `0x01` |
| D-pad right | `1` | `0x02` |
| D-pad left | `1` | `0x04` |
| D-pad up | `1` | `0x08` |
| L | `1` | `0x10` |
| ZL | `1` | `0x20` |
| Minus | `1` | `0x40` |
| Left stick click | `1` | `0x80` |
| Home | `2` | `0x01` |
| C | `2` | `0x02` |
| Capture | `2` | `0x08` |

