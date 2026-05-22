#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>
#include <libusb.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum
{
    NintendoVendorId = 0x057e,
    Switch2ProProductId = 0x2069,
    Switch2UsbInterface = 1,
    MaxReportLength = 64,
    UsbTimeoutMs = 1000,
    UsbAckTimeoutMs = 150
};

typedef struct UsbBridge
{
    libusb_context *context;
    libusb_device_handle *handle;
    uint8_t endpointIn;
    uint8_t endpointOut;
    bool claimed;
} UsbBridge;

typedef struct UdpSink
{
    int socketFd;
    uint64_t sequence;
    bool enabled;
} UdpSink;

typedef struct DecodedState
{
    uint8_t buttons[3];
    int leftX;
    int leftY;
    int rightX;
    int rightY;
    uint8_t leftTrigger;
    uint8_t rightTrigger;
} DecodedState;

typedef struct HidContext
{
    IOHIDDeviceRef device;
    uint8_t report[MaxReportLength];
    uint8_t lastReport[MaxReportLength];
    CFIndex lastReportLength;
    uint32_t lastReportId;
    uint64_t reportCount;
    bool hasLastReport;
    DecodedState lastState;
    bool hasLastState;
} HidContext;

typedef struct UsbCommand
{
    const char *name;
    const uint8_t *data;
    int length;
} UsbCommand;

typedef struct ButtonBit
{
    int byteIndex;
    uint8_t mask;
    const char *name;
} ButtonBit;

static const uint8_t InitCommand03[] = {0x03, 0x91, 0x00, 0x0d, 0x00, 0x08, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const uint8_t UnknownCommand07[] = {0x07, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
static const uint8_t UnknownCommand16[] = {0x16, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
static const uint8_t RequestControllerMac[] = {0x15, 0x91, 0x00, 0x01, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const uint8_t LtkRequest[] = {0x15, 0x91, 0x00, 0x02, 0x00, 0x11, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const uint8_t UnknownCommand15Arg03[] = {0x15, 0x91, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00};
static const uint8_t UnknownCommand09[] = {0x09, 0x91, 0x00, 0x07, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t ImuCommand02[] = {0x0c, 0x91, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00};
static const uint8_t OutUnknownCommand11[] = {0x11, 0x91, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00};
static const uint8_t UnknownCommand0A[] = {0x0a, 0x91, 0x00, 0x08, 0x00, 0x14, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x35, 0x00, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t ImuCommand04[] = {0x0c, 0x91, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00};
static const uint8_t EnableHaptics[] = {0x03, 0x91, 0x00, 0x0a, 0x00, 0x04, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00};
static const uint8_t OutUnknownCommand10[] = {0x10, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
static const uint8_t OutUnknownCommand01[] = {0x01, 0x91, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00};
static const uint8_t OutUnknownCommand03[] = {0x03, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00};
static const uint8_t OutUnknownCommand0AAlt[] = {0x0a, 0x91, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00, 0x00};
static const uint8_t SetPlayerLed[] = {0x09, 0x91, 0x00, 0x07, 0x00, 0x08, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static const UsbCommand InitSequence[] = {
    {"init-input", InitCommand03, sizeof(InitCommand03)},
    {"unknown-07", UnknownCommand07, sizeof(UnknownCommand07)},
    {"unknown-16", UnknownCommand16, sizeof(UnknownCommand16)},
    {"request-mac", RequestControllerMac, sizeof(RequestControllerMac)},
    {"ltk-request", LtkRequest, sizeof(LtkRequest)},
    {"unknown-15-03", UnknownCommand15Arg03, sizeof(UnknownCommand15Arg03)},
    {"unknown-09", UnknownCommand09, sizeof(UnknownCommand09)},
    {"imu-02", ImuCommand02, sizeof(ImuCommand02)},
    {"out-unknown-11", OutUnknownCommand11, sizeof(OutUnknownCommand11)},
    {"unknown-0a", UnknownCommand0A, sizeof(UnknownCommand0A)},
    {"imu-04", ImuCommand04, sizeof(ImuCommand04)},
    {"enable-haptics", EnableHaptics, sizeof(EnableHaptics)},
    {"out-unknown-10", OutUnknownCommand10, sizeof(OutUnknownCommand10)},
    {"out-unknown-01", OutUnknownCommand01, sizeof(OutUnknownCommand01)},
    {"out-unknown-03", OutUnknownCommand03, sizeof(OutUnknownCommand03)},
    {"out-unknown-0a-alt", OutUnknownCommand0AAlt, sizeof(OutUnknownCommand0AAlt)},
    {"set-player-led", SetPlayerLed, sizeof(SetPlayerLed)},
};

static const ButtonBit ButtonMap[] = {
    {0, 0x01, "B"},
    {0, 0x02, "A"},
    {0, 0x04, "Y"},
    {0, 0x08, "X"},
    {0, 0x10, "R"},
    {0, 0x20, "ZR"},
    {0, 0x40, "Plus"},
    {0, 0x80, "RStick"},
    {1, 0x01, "DDown"},
    {1, 0x02, "DRight"},
    {1, 0x04, "DLeft"},
    {1, 0x08, "DUp"},
    {1, 0x10, "L"},
    {1, 0x20, "ZL"},
    {1, 0x40, "Minus"},
    {1, 0x80, "LStick"},
    {2, 0x01, "Home"},
    {2, 0x02, "C"},
    {2, 0x04, "Extra2"},
    {2, 0x08, "Capture"},
    {2, 0x10, "Extra4"},
};

static volatile sig_atomic_t ShouldStop = 0;
static double ListenSeconds = 0.0;
static bool ButtonsOnly = false;
static UdpSink UnityUdp = {.socketFd = -1};

static void HandleSignal(int signalNumber)
{
    (void)signalNumber;
    ShouldStop = 1;
    CFRunLoopStop(CFRunLoopGetMain());
}

static void PrintHex(const uint8_t *data, int length, int maxLength)
{
    int bytesToPrint = length < maxLength ? length : maxLength;
    for (int i = 0; i < bytesToPrint; i++)
        printf("%02x", data[i]);
    if (length > bytesToPrint)
        printf("...");
}

static bool ParseUdpEndpoint(const char *endpoint, char *host, size_t hostLength, char *port, size_t portLength)
{
    const char *separator = strrchr(endpoint, ':');
    if (separator == NULL || separator == endpoint || separator[1] == '\0')
        return false;

    size_t hostPartLength = (size_t)(separator - endpoint);
    size_t portPartLength = strlen(separator + 1);
    if (hostPartLength >= hostLength || portPartLength >= portLength)
        return false;

    memcpy(host, endpoint, hostPartLength);
    host[hostPartLength] = '\0';
    memcpy(port, separator + 1, portPartLength + 1);
    return true;
}

static bool OpenUdpSink(UdpSink *sink, const char *endpoint)
{
    char host[256];
    char port[32];
    if (!ParseUdpEndpoint(endpoint, host, sizeof(host), port, sizeof(port)))
    {
        fprintf(stderr, "Invalid UDP endpoint '%s'. Use host:port, for example 127.0.0.1:28782.\n", endpoint);
        return false;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    struct addrinfo *results = NULL;
    int result = getaddrinfo(host, port, &hints, &results);
    if (result != 0)
    {
        fprintf(stderr, "Could not resolve UDP endpoint %s: %s\n", endpoint, gai_strerror(result));
        return false;
    }

    for (struct addrinfo *candidate = results; candidate != NULL; candidate = candidate->ai_next)
    {
        int socketFd = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (socketFd < 0)
            continue;

        if (connect(socketFd, candidate->ai_addr, candidate->ai_addrlen) == 0)
        {
            sink->socketFd = socketFd;
            sink->enabled = true;
            sink->sequence = 0;
            printf("Streaming Unity bridge packets to udp://%s.\n", endpoint);
            freeaddrinfo(results);
            return true;
        }

        close(socketFd);
    }

    freeaddrinfo(results);
    fprintf(stderr, "Could not open UDP endpoint %s.\n", endpoint);
    return false;
}

static void CloseUdpSink(UdpSink *sink)
{
    if (sink->socketFd >= 0)
        close(sink->socketFd);

    sink->socketFd = -1;
    sink->enabled = false;
}

static const char *UsbErrorName(int result)
{
    return libusb_error_name(result);
}

static int FindUsbEndpoints(UsbBridge *bridge)
{
    libusb_device *device = libusb_get_device(bridge->handle);
    struct libusb_config_descriptor *config = NULL;
    int result = libusb_get_active_config_descriptor(device, &config);
    if (result != LIBUSB_SUCCESS)
        result = libusb_get_config_descriptor(device, 0, &config);

    if (result != LIBUSB_SUCCESS)
    {
        fprintf(stderr, "Could not read USB config descriptor: %s\n", UsbErrorName(result));
        return result;
    }

    printf("USB configuration has %u interface(s).\n", config->bNumInterfaces);

    for (uint8_t i = 0; i < config->bNumInterfaces; i++)
    {
        const struct libusb_interface *interface = &config->interface[i];
        for (int altIndex = 0; altIndex < interface->num_altsetting; altIndex++)
        {
            const struct libusb_interface_descriptor *alt = &interface->altsetting[altIndex];
            printf("  interface %u alt %u class 0x%02x endpoints %u\n",
                   alt->bInterfaceNumber,
                   alt->bAlternateSetting,
                   alt->bInterfaceClass,
                   alt->bNumEndpoints);

            if (alt->bInterfaceNumber != Switch2UsbInterface)
                continue;

            for (uint8_t endpointIndex = 0; endpointIndex < alt->bNumEndpoints; endpointIndex++)
            {
                const struct libusb_endpoint_descriptor *endpoint = &alt->endpoint[endpointIndex];
                uint8_t attributes = endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
                uint8_t address = endpoint->bEndpointAddress;

                if (attributes != LIBUSB_TRANSFER_TYPE_BULK)
                    continue;

                if ((address & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN)
                    bridge->endpointIn = address;
                else
                    bridge->endpointOut = address;
            }
        }
    }

    libusb_free_config_descriptor(config);

    if (bridge->endpointOut == 0)
    {
        fprintf(stderr, "Could not find bulk OUT endpoint on USB interface %d.\n", Switch2UsbInterface);
        return LIBUSB_ERROR_NOT_FOUND;
    }

    printf("Using bulk OUT endpoint 0x%02x", bridge->endpointOut);
    if (bridge->endpointIn != 0)
        printf(" and IN endpoint 0x%02x", bridge->endpointIn);
    printf(".\n");

    return LIBUSB_SUCCESS;
}

static int OpenUsbBridge(UsbBridge *bridge)
{
    memset(bridge, 0, sizeof(*bridge));

    int result = libusb_init(&bridge->context);
    if (result != LIBUSB_SUCCESS)
    {
        fprintf(stderr, "libusb_init failed: %s\n", UsbErrorName(result));
        return result;
    }

    bridge->handle = libusb_open_device_with_vid_pid(
        bridge->context,
        NintendoVendorId,
        Switch2ProProductId);

    if (bridge->handle == NULL)
    {
        fprintf(stderr, "Switch 2 Pro Controller USB device not found.\n");
        return LIBUSB_ERROR_NOT_FOUND;
    }

    struct libusb_device_descriptor descriptor;
    result = libusb_get_device_descriptor(libusb_get_device(bridge->handle), &descriptor);
    if (result == LIBUSB_SUCCESS)
    {
        printf("USB device: VID 0x%04x PID 0x%04x class 0x%02x\n",
               descriptor.idVendor,
               descriptor.idProduct,
               descriptor.bDeviceClass);
    }

    result = FindUsbEndpoints(bridge);
    if (result != LIBUSB_SUCCESS)
        return result;

    result = libusb_set_auto_detach_kernel_driver(bridge->handle, 1);
    if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_NOT_SUPPORTED)
        printf("Auto-detach kernel driver unavailable: %s\n", UsbErrorName(result));

    result = libusb_claim_interface(bridge->handle, Switch2UsbInterface);
    if (result != LIBUSB_SUCCESS)
    {
        fprintf(stderr, "Could not claim USB interface %d: %s\n", Switch2UsbInterface, UsbErrorName(result));
        return result;
    }

    bridge->claimed = true;
    printf("Claimed USB interface %d.\n", Switch2UsbInterface);
    return LIBUSB_SUCCESS;
}

static void CloseUsbBridge(UsbBridge *bridge)
{
    if (bridge->handle != NULL)
    {
        if (bridge->claimed)
            libusb_release_interface(bridge->handle, Switch2UsbInterface);
        libusb_close(bridge->handle);
    }

    if (bridge->context != NULL)
        libusb_exit(bridge->context);

    memset(bridge, 0, sizeof(*bridge));
}

static bool SendUsbCommand(UsbBridge *bridge, const UsbCommand *command)
{
    int transferred = 0;
    int result = libusb_bulk_transfer(
        bridge->handle,
        bridge->endpointOut,
        (unsigned char *)command->data,
        command->length,
        &transferred,
        UsbTimeoutMs);

    printf("  %-18s out ", command->name);
    if (result != LIBUSB_SUCCESS)
    {
        printf("failed: %s\n", UsbErrorName(result));
        return false;
    }

    printf("%2d/%2d bytes", transferred, command->length);

    if (bridge->endpointIn != 0)
    {
        uint8_t ack[MaxReportLength];
        int ackLength = 0;
        usleep(10000);
        result = libusb_bulk_transfer(
            bridge->handle,
            bridge->endpointIn,
            ack,
            sizeof(ack),
            &ackLength,
            UsbAckTimeoutMs);

        if (result == LIBUSB_SUCCESS)
        {
            printf(" ack ");
            PrintHex(ack, ackLength, 32);
        }
        else if (result != LIBUSB_ERROR_TIMEOUT)
        {
            printf(" ack failed: %s", UsbErrorName(result));
        }
    }

    printf("\n");
    fflush(stdout);
    return transferred == command->length;
}

static bool InitializeUsbController(UsbBridge *bridge)
{
    int result = OpenUsbBridge(bridge);
    if (result != LIBUSB_SUCCESS)
        return false;

    printf("Sending Switch 2 Pro Controller USB initialization sequence.\n");
    bool success = true;
    size_t commandCount = sizeof(InitSequence) / sizeof(InitSequence[0]);
    for (size_t i = 0; i < commandCount; i++)
    {
        if (!SendUsbCommand(bridge, &InitSequence[i]))
            success = false;
        usleep(50000);
    }

    printf("USB initialization %s.\n", success ? "complete" : "finished with errors");
    return success;
}

static CFNumberRef CreateIntNumber(int value)
{
    return CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &value);
}

static CFMutableDictionaryRef CreateMatchingDictionary(void)
{
    CFMutableDictionaryRef dict = CFDictionaryCreateMutable(
        kCFAllocatorDefault,
        0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);

    if (dict == NULL)
        return NULL;

    CFNumberRef vendor = CreateIntNumber(NintendoVendorId);
    CFNumberRef product = CreateIntNumber(Switch2ProProductId);

    if (vendor != NULL)
    {
        CFDictionarySetValue(dict, CFSTR(kIOHIDVendorIDKey), vendor);
        CFRelease(vendor);
    }

    if (product != NULL)
    {
        CFDictionarySetValue(dict, CFSTR(kIOHIDProductIDKey), product);
        CFRelease(product);
    }

    return dict;
}

static void PrintCStringProperty(IOHIDDeviceRef device, CFStringRef key, const char *label)
{
    CFTypeRef value = IOHIDDeviceGetProperty(device, key);
    if (value == NULL || CFGetTypeID(value) != CFStringGetTypeID())
        return;

    char buffer[256];
    if (CFStringGetCString((CFStringRef)value, buffer, sizeof(buffer), kCFStringEncodingUTF8))
        printf("%s: %s\n", label, buffer);
}

static void PrintIntProperty(IOHIDDeviceRef device, CFStringRef key, const char *label)
{
    CFTypeRef value = IOHIDDeviceGetProperty(device, key);
    if (value == NULL || CFGetTypeID(value) != CFNumberGetTypeID())
        return;

    int intValue = 0;
    if (CFNumberGetValue((CFNumberRef)value, kCFNumberIntType, &intValue))
        printf("%s: 0x%04x (%d)\n", label, intValue, intValue);
}

static void Unpack12BitTriplet(const uint8_t *data, int *first, int *second)
{
    *first = data[0] | ((data[1] & 0x0f) << 8);
    *second = (data[1] >> 4) | (data[2] << 4);
}

static float NormalizeStick(int value)
{
    float normalized = ((float)value - 2048.0f) / 2048.0f;
    if (normalized < -1.0f)
        return -1.0f;
    if (normalized > 1.0f)
        return 1.0f;
    return normalized;
}

static float Clamp01(float value)
{
    if (value < 0.0f)
        return 0.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

static float NormalizeTrigger(uint8_t rawValue, bool digitalPressed)
{
    if (digitalPressed)
        return 1.0f;
    return Clamp01((float)rawValue / 255.0f);
}

static void SendUnityState(const DecodedState *state)
{
    if (!UnityUdp.enabled || UnityUdp.socketFd < 0)
        return;

    uint32_t rawButtons = (uint32_t)state->buttons[0] |
                          ((uint32_t)state->buttons[1] << 8) |
                          ((uint32_t)state->buttons[2] << 16);
    float leftTrigger = NormalizeTrigger(state->leftTrigger, (state->buttons[1] & 0x20) != 0);
    float rightTrigger = NormalizeTrigger(state->rightTrigger, (state->buttons[0] & 0x20) != 0);
    double timestamp = CFAbsoluteTimeGetCurrent();

    char json[512];
    int length = snprintf(
        json,
        sizeof(json),
        "{\"version\":1,\"sequence\":%llu,\"timestamp\":%.6f,"
        "\"vendorId\":%d,\"productId\":%d,\"buttonMask\":%u,"
        "\"buttons0\":%u,\"buttons1\":%u,\"buttons2\":%u,"
        "\"leftStickX\":%.5f,\"leftStickY\":%.5f,"
        "\"rightStickX\":%.5f,\"rightStickY\":%.5f,"
        "\"leftTrigger\":%.5f,\"rightTrigger\":%.5f}\n",
        (unsigned long long)++UnityUdp.sequence,
        timestamp,
        NintendoVendorId,
        Switch2ProProductId,
        rawButtons,
        state->buttons[0],
        state->buttons[1],
        state->buttons[2],
        NormalizeStick(state->leftX),
        NormalizeStick(state->leftY),
        NormalizeStick(state->rightX),
        NormalizeStick(state->rightY),
        leftTrigger,
        rightTrigger);

    if (length <= 0 || (size_t)length >= sizeof(json))
        return;

    ssize_t sent = send(UnityUdp.socketFd, json, (size_t)length, 0);
    if (sent < 0)
        perror("UDP send failed");
}

static void PrintPressedButtons(const uint8_t *buttons)
{
    bool any = false;
    size_t buttonCount = sizeof(ButtonMap) / sizeof(ButtonMap[0]);
    for (size_t i = 0; i < buttonCount; i++)
    {
        const ButtonBit *button = &ButtonMap[i];
        if ((buttons[button->byteIndex] & button->mask) == 0)
            continue;

        printf("%s%s", any ? " " : "", button->name);
        any = true;
    }

    if (!any)
        printf("none");
}

static bool DecodeReport(uint32_t reportId, const uint8_t *report, CFIndex reportLength, DecodedState *state)
{
    const uint8_t *payload = report;
    CFIndex payloadLength = reportLength;

    if (payloadLength > 0 && reportId != 0 && payload[0] == (uint8_t)reportId)
    {
        payload++;
        payloadLength--;
    }

    if (payloadLength < 14)
        return false;

    const uint8_t *buttons = payload + 0x02;
    const uint8_t *leftStick = payload + 0x05;
    const uint8_t *rightStick = payload + 0x08;

    state->buttons[0] = buttons[0];
    state->buttons[1] = buttons[1];
    state->buttons[2] = buttons[2];
    state->leftTrigger = payload[0x0c];
    state->rightTrigger = payload[0x0d];

    Unpack12BitTriplet(leftStick, &state->leftX, &state->leftY);
    Unpack12BitTriplet(rightStick, &state->rightX, &state->rightY);
    return true;
}

static bool IsMeaningfulStateChange(const DecodedState *previous, const DecodedState *current)
{
    const int stickThreshold = 24;
    const int triggerThreshold = 3;

    if (memcmp(previous->buttons, current->buttons, sizeof(current->buttons)) != 0)
        return true;

    if (abs(previous->leftX - current->leftX) >= stickThreshold ||
        abs(previous->leftY - current->leftY) >= stickThreshold ||
        abs(previous->rightX - current->rightX) >= stickThreshold ||
        abs(previous->rightY - current->rightY) >= stickThreshold)
        return true;

    if (abs((int)previous->leftTrigger - (int)current->leftTrigger) >= triggerThreshold ||
        abs((int)previous->rightTrigger - (int)current->rightTrigger) >= triggerThreshold)
        return true;

    return false;
}

static bool IsButtonOrTriggerChange(const DecodedState *previous, const DecodedState *current)
{
    const int triggerThreshold = 3;

    if (memcmp(previous->buttons, current->buttons, sizeof(current->buttons)) != 0)
        return true;

    if (abs((int)previous->leftTrigger - (int)current->leftTrigger) >= triggerThreshold ||
        abs((int)previous->rightTrigger - (int)current->rightTrigger) >= triggerThreshold)
        return true;

    return false;
}

static void PrintDecodedState(
    uint64_t reportCount,
    uint32_t reportId,
    const uint8_t *report,
    CFIndex reportLength,
    const DecodedState *state)
{
    printf("hid #%llu id=%u len=%ld raw=",
           (unsigned long long)reportCount,
           reportId,
           (long)reportLength);
    PrintHex(report, (int)reportLength, 18);

    printf(" buttons=%02x%02x%02x",
           state->buttons[0],
           state->buttons[1],
           state->buttons[2]);
    printf(" L=%4d,%4d (% .2f,% .2f)",
           state->leftX,
           state->leftY,
           NormalizeStick(state->leftX),
           NormalizeStick(state->leftY));
    printf(" R=%4d,%4d (% .2f,% .2f)",
           state->rightX,
           state->rightY,
           NormalizeStick(state->rightX),
           NormalizeStick(state->rightY));
    printf(" LT=%3u RT=%3u pressed=[", state->leftTrigger, state->rightTrigger);
    PrintPressedButtons(state->buttons);
    printf("]");

    printf("\n");
    fflush(stdout);
}

static void ReportCallback(
    void *context,
    IOReturn result,
    void *sender,
    IOHIDReportType type,
    uint32_t reportId,
    uint8_t *report,
    CFIndex reportLength)
{
    (void)sender;
    (void)type;

    HidContext *hidContext = (HidContext *)context;
    if (hidContext == NULL)
        return;

    hidContext->reportCount++;

    if (result != kIOReturnSuccess)
    {
        printf("hid callback error: 0x%08x\n", result);
        return;
    }

    DecodedState state;
    memset(&state, 0, sizeof(state));

    if (DecodeReport(reportId, report, reportLength, &state))
    {
        if (hidContext->hasLastState)
        {
            bool shouldPrint = ButtonsOnly
                                   ? IsButtonOrTriggerChange(&hidContext->lastState, &state)
                                   : IsMeaningfulStateChange(&hidContext->lastState, &state);
            if (!shouldPrint)
                return;
        }

        hidContext->hasLastState = true;
        hidContext->lastState = state;

        PrintDecodedState(hidContext->reportCount, reportId, report, reportLength, &state);
        SendUnityState(&state);
        return;
    }

    bool rawChanged = !hidContext->hasLastReport ||
                      hidContext->lastReportId != reportId ||
                      hidContext->lastReportLength != reportLength ||
                      memcmp(hidContext->lastReport, report, (size_t)reportLength) != 0;

    if (!rawChanged)
        return;

    hidContext->hasLastReport = true;
    hidContext->lastReportId = reportId;
    hidContext->lastReportLength = reportLength;
    memcpy(hidContext->lastReport, report, (size_t)reportLength);

    printf("hid #%llu id=%u len=%ld raw=",
           (unsigned long long)hidContext->reportCount,
           reportId,
           (long)reportLength);
    PrintHex(report, (int)reportLength, 18);
    printf("\n");
    fflush(stdout);
}

static bool RunHidListener(void)
{
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (manager == NULL)
    {
        fprintf(stderr, "Failed to create IOHIDManager.\n");
        return false;
    }

    CFMutableDictionaryRef match = CreateMatchingDictionary();
    if (match == NULL)
    {
        fprintf(stderr, "Failed to create HID matching dictionary.\n");
        CFRelease(manager);
        return false;
    }

    IOHIDManagerSetDeviceMatching(manager, match);
    CFRelease(match);
    IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetMain(), kCFRunLoopDefaultMode);

    IOReturn openResult = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
    if (openResult != kIOReturnSuccess)
    {
        fprintf(stderr, "IOHIDManagerOpen failed: 0x%08x\n", openResult);
        CFRelease(manager);
        return false;
    }

    CFSetRef devices = IOHIDManagerCopyDevices(manager);
    if (devices == NULL || CFSetGetCount(devices) == 0)
    {
        fprintf(stderr, "No Switch 2 Pro Controller HID devices found.\n");
        if (devices != NULL)
            CFRelease(devices);
        CFRelease(manager);
        return false;
    }

    CFIndex deviceCount = CFSetGetCount(devices);
    IOHIDDeviceRef *deviceRefs = calloc((size_t)deviceCount, sizeof(IOHIDDeviceRef));
    HidContext *contexts = calloc((size_t)deviceCount, sizeof(HidContext));

    if (deviceRefs == NULL || contexts == NULL)
    {
        fprintf(stderr, "Out of memory.\n");
        free(deviceRefs);
        free(contexts);
        CFRelease(devices);
        CFRelease(manager);
        return false;
    }

    CFSetGetValues(devices, (const void **)deviceRefs);
    printf("Found %ld matching HID device(s).\n", (long)deviceCount);

    for (CFIndex i = 0; i < deviceCount; i++)
    {
        IOHIDDeviceRef device = deviceRefs[i];
        contexts[i].device = device;

        printf("\nHID device %ld\n", (long)(i + 1));
        PrintCStringProperty(device, CFSTR(kIOHIDProductKey), "Product");
        PrintCStringProperty(device, CFSTR(kIOHIDManufacturerKey), "Manufacturer");
        PrintCStringProperty(device, CFSTR(kIOHIDTransportKey), "Transport");
        PrintIntProperty(device, CFSTR(kIOHIDVendorIDKey), "VendorID");
        PrintIntProperty(device, CFSTR(kIOHIDProductIDKey), "ProductID");
        PrintIntProperty(device, CFSTR(kIOHIDPrimaryUsagePageKey), "PrimaryUsagePage");
        PrintIntProperty(device, CFSTR(kIOHIDPrimaryUsageKey), "PrimaryUsage");

        IOReturn deviceOpenResult = IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone);
        printf("IOHIDDeviceOpen: 0x%08x\n", deviceOpenResult);

        IOHIDDeviceRegisterInputReportCallback(
            device,
            contexts[i].report,
            MaxReportLength,
            ReportCallback,
            &contexts[i]);

        IOHIDDeviceScheduleWithRunLoop(device, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
    }

    printf("\nListening for changed HID reports. Press buttons/move sticks.");
    if (ListenSeconds > 0.0)
        printf(" Auto-stopping after %.1f second(s).", ListenSeconds);
    printf("\nPress Ctrl-C to stop.\n");
    fflush(stdout);

    CFAbsoluteTime endTime = CFAbsoluteTimeGetCurrent() + ListenSeconds;
    while (!ShouldStop)
    {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.25, false);
        if (ListenSeconds > 0.0 && CFAbsoluteTimeGetCurrent() >= endTime)
            break;
    }

    printf("\nStopping HID listener.\n");

    for (CFIndex i = 0; i < deviceCount; i++)
    {
        IOHIDDeviceUnscheduleFromRunLoop(deviceRefs[i], CFRunLoopGetMain(), kCFRunLoopDefaultMode);
        IOHIDDeviceClose(deviceRefs[i], kIOHIDOptionsTypeNone);
    }

    free(deviceRefs);
    free(contexts);
    CFRelease(devices);
    CFRelease(manager);
    return true;
}

static void PrintUsage(const char *programName)
{
    printf("Usage: %s [--init-only] [--listen-only] [--buttons-only] [--seconds N] [--udp HOST:PORT]\n", programName);
}

int main(int argc, char **argv)
{
    bool initOnly = false;
    bool listenOnly = false;
    const char *udpEndpoint = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--init-only") == 0)
            initOnly = true;
        else if (strcmp(argv[i], "--listen-only") == 0)
            listenOnly = true;
        else if (strcmp(argv[i], "--buttons-only") == 0)
            ButtonsOnly = true;
        else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc)
            ListenSeconds = atof(argv[++i]);
        else if (strcmp(argv[i], "--udp") == 0 && i + 1 < argc)
            udpEndpoint = argv[++i];
        else if (strcmp(argv[i], "--help") == 0)
        {
            PrintUsage(argv[0]);
            return 0;
        }
        else
        {
            PrintUsage(argv[0]);
            return 2;
        }
    }

    if (initOnly && listenOnly)
    {
        fprintf(stderr, "--init-only and --listen-only cannot be combined.\n");
        return 2;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGINT, HandleSignal);
    signal(SIGTERM, HandleSignal);

    if (udpEndpoint != NULL && !OpenUdpSink(&UnityUdp, udpEndpoint))
        return 1;

    UsbBridge bridge;
    memset(&bridge, 0, sizeof(bridge));

    if (!listenOnly)
    {
        if (!InitializeUsbController(&bridge))
        {
            CloseUsbBridge(&bridge);
            CloseUdpSink(&UnityUdp);
            return 1;
        }
    }

    bool hidOk = true;
    if (!initOnly)
        hidOk = RunHidListener();

    CloseUsbBridge(&bridge);
    CloseUdpSink(&UnityUdp);
    return hidOk ? 0 : 1;
}
