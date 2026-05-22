# Switch2 Controller Lab for Unity

Switch2 Controller Lab for Unity is a Unity Package Manager package for Unity
6.x projects using the Unity Input System.

It receives controller state from the Switch2 Controller Lab native macOS helper
over localhost UDP and exposes that state as a virtual `Gamepad`. Game code can
then use ordinary Input System bindings such as `<Gamepad>/leftStick`,
`<Gamepad>/buttonSouth`, `<Gamepad>/rightTrigger`, and `<Gamepad>/start`.

## Requirements

- Unity 6.x
- Unity Input System package
- Native helper from `Switch2ControllerLab`
- USB-connected Nintendo Switch 2 Pro Controller

The package declares `com.unity.inputsystem` `1.8.0` as its minimum dependency.
Newer Unity Input System 1.x versions are expected to work.

## Install

1. Open a Unity 6.x project.
2. Open `Window > Package Manager`.
3. Click `+`.
4. Choose `Add package from disk...`.
5. Select:

   ```text
   Switch2ControllerLabUnity/com.switch2controllerlab.unitybridge/package.json
   ```

6. Add an empty GameObject to a startup scene.
7. Add the `Switch2BridgeReceiver` component.
8. Keep the default UDP port `28782`, or set the component to a matching custom
   port.

Optional: import the `Debug Overlay` sample from the package's Samples section.
Add `Switch2BridgeDebugOverlay` to any scene object to view live status and
input values.

## Run The Native Helper

From the repository root:

```sh
Switch2ControllerLab/switch2_mac_bridge --udp 127.0.0.1:28782
```

For a short smoke test:

```sh
Switch2ControllerLab/switch2_mac_bridge --udp 127.0.0.1:28782 --seconds 15
```

## Unity Behavior

At runtime, `Switch2BridgeReceiver`:

1. Opens a UDP listener on the configured port.
2. Creates a virtual Input System `Gamepad` named `Switch2 Controller Lab`.
3. Parses incoming JSON packets on Unity's main thread.
4. Converts packets into `GamepadState`.
5. Queues state events through the Unity Input System.

No project-specific input code is required. Existing Input Actions can bind to
standard `Gamepad` controls.

## Input Mapping

The bridge maps physical Nintendo button positions to Unity's location-based
Gamepad controls:

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

Home, Capture, C, and extra buttons are preserved in the raw packet fields but
are not exposed on generic Unity `Gamepad` controls.

## Packet Format

The native helper sends one JSON object per UDP datagram:

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

`Switch2BridgePacket` mirrors this format.

## Public API

Main runtime types:

- `Switch2BridgeReceiver`: MonoBehaviour that receives UDP packets and queues
  virtual gamepad state.
- `Switch2BridgePacket`: serializable packet model.
- `Switch2BridgeButtons`: known raw button bit constants.

Namespace:

```csharp
using Switch2ControllerLab.UnityBridge;
```

