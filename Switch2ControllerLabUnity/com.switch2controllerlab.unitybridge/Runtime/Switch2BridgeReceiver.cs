using System;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using UnityEngine;
using UnityEngine.InputSystem;
using UnityEngine.InputSystem.LowLevel;

namespace Switch2ControllerLab.UnityBridge
{
    [DefaultExecutionOrder(-1000)]
    public sealed class Switch2BridgeReceiver : MonoBehaviour
    {
        public const int DefaultPort = 28782;

        [SerializeField] private int listenPort = DefaultPort;
        [SerializeField] private bool startOnEnable = true;
        [SerializeField] private bool keepVirtualDeviceOnDisable;
        [SerializeField] private bool logPackets;
        [SerializeField] private float staleAfterSeconds = 1.0f;

        private readonly ConcurrentQueue<string> _packetQueue = new ConcurrentQueue<string>();
        private UdpClient _udpClient;
        private Thread _listenerThread;
        private volatile bool _running;
        private Gamepad _virtualGamepad;
        private Switch2BridgePacket _latestPacket;
        private float _lastPacketRealtime = -999.0f;
        private string _lastError;

        public int ListenPort => listenPort;
        public bool IsListening => _running;
        public bool HasVirtualGamepad => _virtualGamepad != null && _virtualGamepad.added;
        public bool HasRecentPacket => Time.realtimeSinceStartup - _lastPacketRealtime <= staleAfterSeconds;
        public Switch2BridgePacket LatestPacket => _latestPacket;
        public string LastError => _lastError;
        public Gamepad VirtualGamepad => _virtualGamepad;

        private void OnEnable()
        {
            if (startOnEnable)
                StartListening();
        }

        private void Update()
        {
            EnsureVirtualGamepad();

            int processed = 0;
            while (processed < 128 && _packetQueue.TryDequeue(out string json))
            {
                processed++;
                ProcessPacket(json);
            }
        }

        private void OnDisable()
        {
            StopListening();

            if (!keepVirtualDeviceOnDisable)
                RemoveVirtualGamepad();
        }

        private void OnDestroy()
        {
            StopListening();
            RemoveVirtualGamepad();
        }

        public void StartListening()
        {
            if (_running)
                return;

            try
            {
                _udpClient = new UdpClient(listenPort);
                _running = true;
                _listenerThread = new Thread(ReceiveLoop)
                {
                    IsBackground = true,
                    Name = "Switch2 Controller Lab UDP Receiver"
                };
                _listenerThread.Start();
                _lastError = null;
            }
            catch (Exception exception)
            {
                _running = false;
                _lastError = exception.Message;
                Debug.LogError($"Switch 2 bridge failed to listen on UDP port {listenPort}: {exception.Message}", this);
            }
        }

        public void StopListening()
        {
            _running = false;

            try
            {
                _udpClient?.Close();
            }
            catch (ObjectDisposedException)
            {
            }

            if (_listenerThread != null && _listenerThread.IsAlive)
                _listenerThread.Join(250);

            _listenerThread = null;
            _udpClient = null;
        }

        private void ReceiveLoop()
        {
            IPEndPoint remoteEndpoint = new IPEndPoint(IPAddress.Any, 0);

            while (_running)
            {
                try
                {
                    byte[] bytes = _udpClient.Receive(ref remoteEndpoint);
                    string json = Encoding.UTF8.GetString(bytes);
                    _packetQueue.Enqueue(json);
                }
                catch (SocketException)
                {
                    if (_running)
                        _lastError = "UDP socket closed unexpectedly.";
                }
                catch (ObjectDisposedException)
                {
                    break;
                }
                catch (Exception exception)
                {
                    _lastError = exception.Message;
                }
            }
        }

        private void ProcessPacket(string json)
        {
            Switch2BridgePacket packet;
            try
            {
                packet = JsonUtility.FromJson<Switch2BridgePacket>(json);
            }
            catch (Exception exception)
            {
                _lastError = $"Bad bridge packet: {exception.Message}";
                return;
            }

            if (!packet.IsValid)
                return;

            _latestPacket = packet;
            _lastPacketRealtime = Time.realtimeSinceStartup;

            GamepadState state = ToGamepadState(packet);
            InputSystem.QueueStateEvent(_virtualGamepad, state);

            if (logPackets)
                Debug.Log($"Switch 2 packet {packet.sequence}: buttons={packet.buttonMask} left=({packet.leftStickX:0.00},{packet.leftStickY:0.00})", this);
        }

        private void EnsureVirtualGamepad()
        {
            if (_virtualGamepad != null && _virtualGamepad.added)
                return;

            _virtualGamepad = InputSystem.AddDevice<Gamepad>("Switch2 Controller Lab");
            _lastError = null;
        }

        private void RemoveVirtualGamepad()
        {
            if (_virtualGamepad == null)
                return;

            if (_virtualGamepad.added)
                InputSystem.RemoveDevice(_virtualGamepad);

            _virtualGamepad = null;
        }

        private static GamepadState ToGamepadState(Switch2BridgePacket packet)
        {
            GamepadState state = new GamepadState
            {
                leftStick = new Vector2(Clamp(packet.leftStickX, -1.0f, 1.0f), Clamp(packet.leftStickY, -1.0f, 1.0f)),
                rightStick = new Vector2(Clamp(packet.rightStickX, -1.0f, 1.0f), Clamp(packet.rightStickY, -1.0f, 1.0f)),
                leftTrigger = Clamp01(packet.leftTrigger),
                rightTrigger = Clamp01(packet.rightTrigger)
            };

            state = state.WithButton(GamepadButton.South, Has(packet.buttons0, Switch2BridgeButtons.B));
            state = state.WithButton(GamepadButton.East, Has(packet.buttons0, Switch2BridgeButtons.A));
            state = state.WithButton(GamepadButton.West, Has(packet.buttons0, Switch2BridgeButtons.Y));
            state = state.WithButton(GamepadButton.North, Has(packet.buttons0, Switch2BridgeButtons.X));
            state = state.WithButton(GamepadButton.RightShoulder, Has(packet.buttons0, Switch2BridgeButtons.R));
            state = state.WithButton(GamepadButton.Start, Has(packet.buttons0, Switch2BridgeButtons.Plus));
            state = state.WithButton(GamepadButton.RightStick, Has(packet.buttons0, Switch2BridgeButtons.RightStick));

            state = state.WithButton(GamepadButton.DpadDown, Has(packet.buttons1, Switch2BridgeButtons.DpadDown));
            state = state.WithButton(GamepadButton.DpadRight, Has(packet.buttons1, Switch2BridgeButtons.DpadRight));
            state = state.WithButton(GamepadButton.DpadLeft, Has(packet.buttons1, Switch2BridgeButtons.DpadLeft));
            state = state.WithButton(GamepadButton.DpadUp, Has(packet.buttons1, Switch2BridgeButtons.DpadUp));
            state = state.WithButton(GamepadButton.LeftShoulder, Has(packet.buttons1, Switch2BridgeButtons.L));
            state = state.WithButton(GamepadButton.Select, Has(packet.buttons1, Switch2BridgeButtons.Minus));
            state = state.WithButton(GamepadButton.LeftStick, Has(packet.buttons1, Switch2BridgeButtons.LeftStick));

            return state;
        }

        private static bool Has(int value, int mask)
        {
            return (value & mask) != 0;
        }

        private static float Clamp01(float value)
        {
            return Clamp(value, 0.0f, 1.0f);
        }

        private static float Clamp(float value, float min, float max)
        {
            if (value < min)
                return min;
            if (value > max)
                return max;
            return value;
        }
    }
}
