# Switch2 Controller Lab

Switch2 Controller Lab is an experimental macOS and Unity bridge for the
Nintendo Switch 2 Pro Controller over USB.

The project has three parts:

- `Switch2ControllerLab`: native macOS C command-line tools for USB
  initialization, HID decoding, and localhost streaming.
- `Switch2ControllerLabUnity`: a Unity 6 package that receives the localhost
  stream and exposes a virtual Unity Input System `Gamepad`.
- `Switch2ControllerLabApp`: a SwiftUI macOS app that wraps the helper and
  provides the future installer target.

This project is not affiliated with Nintendo.

## Current Status

Working:

- macOS detects the controller over USB.
- The native helper sends the controller's USB initialization sequence.
- The helper reads 64-byte HID reports with report ID `9`.
- Sticks, triggers, and common buttons are decoded.
- The helper can stream normalized JSON state over UDP.
- The Unity package receives UDP packets and queues `GamepadState` events into
  the Unity Input System.

Included app prototype:

- SwiftUI macOS status app.
- Live controller diagram.
- App-managed Unity bridge start/stop.
- Optional mouse mode for pointer, click, and scroll control.

Planned:

- Saved app settings and profiles.
- Local `.app` and `.pkg` packaging.

## Supported Environment

Tested with:

- macOS on Apple Silicon
- USB connection
- Nintendo Switch 2 Pro Controller
- Unity 6.x
- Unity Input System 1.x

The native helper uses:

- IOKit HID APIs
- CoreFoundation
- `libusb-1.0`

## Device Details

Known USB/HID identity:

- Vendor ID: `0x057e`
- Product ID: `0x2069`
- Product name: `Switch 2 Pro Controller`
- Transport: `USB`
- HID usage page: `0x01`
- HID usage: `0x05` / gamepad

The controller does not stream useful HID input immediately after connecting.
It first needs a USB bulk initialization sequence on interface `1`. After that
handshake, it sends 64-byte HID reports with report ID `9`.

## Repository Layout

```text
README.md
Switch2ControllerLab/
  README.md
  switch2_hid_probe.c
  switch2_mac_bridge.c
Switch2ControllerLabUnity/
  README.md
  com.switch2controllerlab.unitybridge/
    package.json
    Runtime/
    Samples~/
Switch2ControllerLabApp/
  README.md
```

## Build The Native Helper

Install dependencies:

```sh
brew install libusb pkg-config
```

Build with `pkg-config`:

```sh
clang -Wall -Wextra -Wpedantic -std=c11 \
  Switch2ControllerLab/switch2_mac_bridge.c \
  $(pkg-config --cflags --libs libusb-1.0) \
  -framework IOKit -framework CoreFoundation \
  -o Switch2ControllerLab/switch2_mac_bridge
```

If your `libusb` install is Intel Homebrew under `/usr/local` on an Apple
Silicon Mac, build the helper as a Rosetta binary:

```sh
clang -arch x86_64 -Wall -Wextra -Wpedantic -std=c11 \
  Switch2ControllerLab/switch2_mac_bridge.c \
  -I/usr/local/Cellar/libusb/1.0.29/include/libusb-1.0 \
  -L/usr/local/Cellar/libusb/1.0.29/lib -lusb-1.0 \
  -framework IOKit -framework CoreFoundation \
  -o Switch2ControllerLab/switch2_mac_bridge
```

## Run The Native Helper

Print decoded controller input:

```sh
Switch2ControllerLab/switch2_mac_bridge --seconds 10
```

Stream to the Unity bridge:

```sh
Switch2ControllerLab/switch2_mac_bridge --udp 127.0.0.1:28782
```

Button-focused test:

```sh
Switch2ControllerLab/switch2_mac_bridge --buttons-only --seconds 10
```

Depending on local macOS permissions, USB/HID access may require running from a
Terminal session with appropriate privileges.

## Install The Unity Package

1. Open a Unity 6.x project.
2. Open `Window > Package Manager`.
3. Click `+`.
4. Choose `Add package from disk...`.
5. Select:

   ```text
   Switch2ControllerLabUnity/com.switch2controllerlab.unitybridge/package.json
   ```

6. Add `Switch2BridgeReceiver` to a GameObject in your startup scene.
7. Run the native helper with `--udp 127.0.0.1:28782`.

Unity will see a virtual Input System `Gamepad` named `Switch2 Controller Lab`.

## Unity Input Mapping

| Switch 2 Pro | Unity Input System |
| --- | --- |
| Left stick | `<Gamepad>/leftStick` |
| Right stick | `<Gamepad>/rightStick` |
| B | `<Gamepad>/buttonSouth` |
| A | `<Gamepad>/buttonEast` |
| Y | `<Gamepad>/buttonWest` |
| X | `<Gamepad>/buttonNorth` |
| L | `<Gamepad>/leftShoulder` |
| R | `<Gamepad>/rightShoulder` |
| ZL | `<Gamepad>/leftTrigger` |
| ZR | `<Gamepad>/rightTrigger` |
| Minus | `<Gamepad>/select` |
| Plus | `<Gamepad>/start` |
| D-pad | `<Gamepad>/dpad` |
| Stick clicks | `<Gamepad>/leftStickPress`, `<Gamepad>/rightStickPress` |

## UDP Packet Format

The helper sends one JSON object per UDP datagram:

```json
{
  "version": 1,
  "sequence": 1,
  "timestamp": 769167220.123456,
  "vendorId": 1406,
  "productId": 8297,
  "buttonMask": 34,
  "buttons0": 34,
  "buttons1": 0,
  "buttons2": 0,
  "leftStickX": 0.01,
  "leftStickY": 0.08,
  "rightStickX": 0.09,
  "rightStickY": 0.0,
  "leftTrigger": 0.0,
  "rightTrigger": 1.0
}
```

## macOS App

The SwiftUI app wraps the native helper into a user interface:

- Connection and handshake status.
- Live button/stick/trigger visualization.
- Unity bridge port and streaming controls.
- Optional mouse mode with configurable stick, click, and scroll mappings.
- Local app packaging.

Build it with:

```sh
Switch2ControllerLabApp/build_app.sh
```

The app bundle is written to:

```text
Switch2ControllerLabApp/dist/Switch2 Controller Lab.app
```

System-wide virtual gamepad support is intentionally deferred because it
requires a much more complex DriverKit/system-extension path.

## License

Add a license before publishing this as a public repository.
