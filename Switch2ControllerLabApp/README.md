# Switch2 Controller Lab macOS App

This folder is reserved for the planned Switch2 Controller Lab macOS app.

The command-line helper already proves the USB initialization, HID decoding,
and Unity bridge path. The app should wrap that functionality in a user-facing
SwiftUI interface and eventually provide a local installer package.

## Goals

Initial app:

- Show USB connection status.
- Show controller initialization/handshake status.
- Start and stop Unity bridge streaming.
- Configure the UDP host and port.
- Display a live controller diagram.
- Highlight pressed buttons.
- Show stick and trigger meters.

Optional input utility mode:

- Use the left stick as a mouse pointer.
- Configure left-click and right-click buttons.
- Use triggers or shoulders for scrolling.
- Configure sensitivity, acceleration, and deadzones.
- Save mapping profiles.

## Suggested Architecture

The app should reuse the same core logic as `switch2_mac_bridge`:

- USB enumeration and initialization through `libusb`.
- HID report handling through IOKit.
- Controller decoding shared with the command-line helper.
- UDP bridge output shared with the command-line helper.

The app should not permanently depend on shelling out to the CLI. A reasonable
first prototype can launch the helper process, but the production version should
share code through a small native C module or library that Swift can call.

## macOS Permissions

Mouse mode will require macOS Accessibility permission because it needs to post
system input events.

Relevant APIs:

- `CGEventCreateMouseEvent`
- `CGEventCreateScrollWheelEvent`
- `CGEventPost`

## Packaging Plan

Development milestones:

1. Build an unsigned local `.app`.
2. Add a SwiftUI status window or menu-bar UI.
3. Embed or link the native controller core.
4. Add app-side settings storage.
5. Package as a local `.pkg` with `pkgbuild` and `productbuild`.
6. Add code signing and notarization when distributing beyond local testing.

System-wide virtual gamepad support is intentionally separate from the Unity
bridge. A virtual gamepad driver would require DriverKit or system-extension
work, signing, entitlements, and a heavier install path.

