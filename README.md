# Switch 2 Pro Controller Lab

This folder contains small macOS probes for testing a Nintendo Switch 2 Pro
Controller over USB before Unity has native support for it.

## Current Finding

Yes, we can make a local tool that allows the controller to work for Maelstrom 2
input testing.

macOS sees the controller as:

- Vendor ID: `0x057e` / Nintendo
- Product ID: `0x2069`
- Product: `Switch 2 Pro Controller`
- Transport: `USB`
- HID usage page: `0x01`
- HID usage: `0x05` / gamepad

The controller does not send useful HID input immediately. It needs a USB bulk
initialization sequence on interface `1`. After that handshake, the controller
streams 64-byte HID input reports with report ID `9`.

## Hardware Test Results

The combined bridge successfully:

- Claimed USB interface `1`
- Used bulk OUT endpoint `0x02`
- Used bulk IN endpoint `0x82`
- Sent the full initialization sequence and received ACKs
- Opened the macOS HID device
- Decoded live stick, trigger, and button state

Confirmed button bits from a live capture:

- `A`: byte 0, mask `0x02`
- `R`: byte 0, mask `0x10`
- `ZR`: byte 0, mask `0x20`
- `L`: byte 1, mask `0x10`
- `ZL`: byte 1, mask `0x20`

Stick data is packed as two 12-bit values per three bytes:

```c
x = data[0] | ((data[1] & 0x0f) << 8);
y = (data[1] >> 4) | (data[2] << 4);
```

## Build

The current local `libusb` install is Intel Homebrew under `/usr/local`, so the
bridge is built as an `x86_64` Rosetta binary on this M3 Mac:

```sh
clang -arch x86_64 -Wall -Wextra -Wpedantic -std=c11 \
  Tools/Switch2ControllerLab/switch2_mac_bridge.c \
  -I/usr/local/Cellar/libusb/1.0.29/include/libusb-1.0 \
  -L/usr/local/Cellar/libusb/1.0.29/lib -lusb-1.0 \
  -framework IOKit -framework CoreFoundation \
  -o Tools/Switch2ControllerLab/switch2_mac_bridge
```

If Apple Silicon Homebrew `libusb` is installed later, this can be rebuilt as a
native arm64 binary using `/opt/homebrew` include/lib paths instead.

## Run

Initialize the controller and listen for meaningful input changes:

```sh
Tools/Switch2ControllerLab/switch2_mac_bridge --seconds 10
```

Quiet button-focused capture:

```sh
Tools/Switch2ControllerLab/switch2_mac_bridge --buttons-only --seconds 10
```

Only send the USB initialization sequence:

```sh
Tools/Switch2ControllerLab/switch2_mac_bridge --init-only
```

Only listen to HID reports after another tool/page has initialized the
controller:

```sh
Tools/Switch2ControllerLab/switch2_mac_bridge --listen-only
```

When run from Codex, the tool needs elevated hardware access. From a normal
Terminal session, try it directly first; if macOS denies USB/HID access, run it
with appropriate local permissions.

## Recommended Unity Path

For Maelstrom 2, do not start by trying to make a system-wide virtual gamepad on
macOS. That path leads into DriverKit/system extensions/signing.

Use a helper bridge first:

1. Native helper initializes the controller and reads HID reports.
2. Helper publishes normalized input over `localhost`, likely UDP or WebSocket.
3. Unity reads that stream and maps it into the Input System as a development
   device or a project-local input provider.
4. When official SDL/Unity/macOS support matures, swap the backend while keeping
   the same in-game action map.

Suggested Maelstrom 2 action map:

- Left stick: ship pitch/yaw or planar movement depending on camera mode
- Right stick: aim/camera
- ZR: primary fire
- ZL: secondary fire or brake/strafe modifier
- R/L: roll, weapon cycle, or boost depending on flight model
- A/B/X/Y: interact, boost, ordnance, target
- Plus/Minus: pause/map
- C/Home/Capture/extra buttons: keep reserved until final platform behavior is
  better understood

