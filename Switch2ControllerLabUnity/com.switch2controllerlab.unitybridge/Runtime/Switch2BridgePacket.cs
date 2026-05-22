using System;

namespace Switch2ControllerLab.UnityBridge
{
    [Serializable]
    public struct Switch2BridgePacket
    {
        public int version;
        public long sequence;
        public float timestamp;
        public int vendorId;
        public int productId;
        public int buttonMask;
        public int buttons0;
        public int buttons1;
        public int buttons2;
        public float leftStickX;
        public float leftStickY;
        public float rightStickX;
        public float rightStickY;
        public float leftTrigger;
        public float rightTrigger;

        public bool IsValid => version == 1;
    }
}
