# Switch2 Controller Lab Unity Package

This folder contains the Unity side of Switch2 Controller Lab.

The package receives localhost UDP packets from the native macOS helper and
exposes a virtual Unity Input System `Gamepad`. It is intended to be installed
as a local Unity Package Manager package in any Unity 6.x project.

Package path:

```text
Switch2ControllerLabUnity/com.switch2controllerlab.unitybridge
```

Package manifest:

```text
Switch2ControllerLabUnity/com.switch2controllerlab.unitybridge/package.json
```

## Quick Start

1. Build the native helper from `Switch2ControllerLab`.
2. Install this package in Unity with `Add package from disk...`.
3. Add `Switch2BridgeReceiver` to a GameObject in your startup scene.
4. Run:

   ```sh
   Switch2ControllerLab/switch2_mac_bridge --udp 127.0.0.1:28782
   ```

The Unity Input System will see a virtual `Gamepad` named
`Switch2 Controller Lab`.

See the package README for full installation and mapping details:

```text
Switch2ControllerLabUnity/com.switch2controllerlab.unitybridge/README.md
```

