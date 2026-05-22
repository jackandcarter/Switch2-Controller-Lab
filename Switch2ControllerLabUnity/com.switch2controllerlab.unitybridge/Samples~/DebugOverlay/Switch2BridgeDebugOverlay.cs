using Switch2ControllerLab.UnityBridge;
using UnityEngine;

public sealed class Switch2BridgeDebugOverlay : MonoBehaviour
{
    [SerializeField] private Switch2BridgeReceiver receiver;
    [SerializeField] private Rect windowRect = new Rect(16, 16, 360, 180);

    private void Awake()
    {
        if (receiver == null)
            receiver = FindFirstObjectByType<Switch2BridgeReceiver>();
    }

    private void OnGUI()
    {
        if (receiver == null)
            return;

        windowRect = GUI.Window(GetInstanceID(), windowRect, DrawWindow, "Switch2 Controller Lab");
    }

    private void DrawWindow(int id)
    {
        Switch2BridgePacket packet = receiver.LatestPacket;
        GUILayout.Label($"UDP: {(receiver.IsListening ? "listening" : "stopped")} :{receiver.ListenPort}");
        GUILayout.Label($"Device: {(receiver.HasVirtualGamepad ? "virtual Gamepad active" : "not created")}");
        GUILayout.Label($"Input: {(receiver.HasRecentPacket ? "live" : "waiting")}");

        if (!string.IsNullOrEmpty(receiver.LastError))
            GUILayout.Label($"Error: {receiver.LastError}");

        GUILayout.Space(6);
        GUILayout.Label($"Seq: {packet.sequence}");
        GUILayout.Label($"Buttons: {packet.buttons0:X2} {packet.buttons1:X2} {packet.buttons2:X2}");
        GUILayout.Label($"Left Stick:  {packet.leftStickX:0.00}, {packet.leftStickY:0.00}");
        GUILayout.Label($"Right Stick: {packet.rightStickX:0.00}, {packet.rightStickY:0.00}");
        GUILayout.Label($"Triggers: L {packet.leftTrigger:0.00}  R {packet.rightTrigger:0.00}");

        GUI.DragWindow();
    }
}
