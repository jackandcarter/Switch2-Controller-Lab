#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum
{
    NintendoVendorId = 0x057e,
    Switch2ProProductId = 0x2069,
    MaxReportLength = 64
};

typedef struct DeviceContext
{
    IOHIDDeviceRef device;
    uint8_t report[MaxReportLength];
    int reportCount;
} DeviceContext;

static volatile sig_atomic_t shouldStop = 0;

static void HandleSignal(int signalNumber)
{
    (void)signalNumber;
    shouldStop = 1;
    CFRunLoopStop(CFRunLoopGetMain());
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

static void ReportCallback(
    void *context,
    IOReturn result,
    void *sender,
    IOHIDReportType type,
    uint32_t reportID,
    uint8_t *report,
    CFIndex reportLength)
{
    (void)sender;

    DeviceContext *deviceContext = (DeviceContext *)context;
    if (deviceContext == NULL)
        return;

    deviceContext->reportCount++;

    printf("report #%d result=0x%08x type=%ld id=%u len=%ld data=",
           deviceContext->reportCount,
           result,
           (long)type,
           reportID,
           (long)reportLength);

    CFIndex bytesToPrint = reportLength < 24 ? reportLength : 24;
    for (CFIndex i = 0; i < bytesToPrint; i++)
        printf("%02x", report[i]);

    if (reportLength > bytesToPrint)
        printf("...");

    printf("\n");
    fflush(stdout);
}

static void ValueCallback(void *context, IOReturn result, void *sender, IOHIDValueRef value)
{
    (void)context;
    (void)sender;

    IOHIDElementRef element = IOHIDValueGetElement(value);
    if (element == NULL)
        return;

    uint32_t usagePage = IOHIDElementGetUsagePage(element);
    uint32_t usage = IOHIDElementGetUsage(element);
    CFIndex integerValue = IOHIDValueGetIntegerValue(value);

    if (usagePage == kHIDPage_GenericDesktop || usagePage == kHIDPage_Button)
    {
        printf("value result=0x%08x page=0x%02x usage=0x%02x value=%ld\n",
               result,
               usagePage,
               usage,
               (long)integerValue);
        fflush(stdout);
    }
}

int main(void)
{
    signal(SIGINT, HandleSignal);
    signal(SIGTERM, HandleSignal);

    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (manager == NULL)
    {
        fprintf(stderr, "Failed to create IOHIDManager.\n");
        return 1;
    }

    CFMutableDictionaryRef match = CreateMatchingDictionary();
    if (match == NULL)
    {
        fprintf(stderr, "Failed to create matching dictionary.\n");
        CFRelease(manager);
        return 1;
    }

    IOHIDManagerSetDeviceMatching(manager, match);
    CFRelease(match);

    IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetMain(), kCFRunLoopDefaultMode);

    IOReturn openResult = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
    if (openResult != kIOReturnSuccess)
    {
        fprintf(stderr, "IOHIDManagerOpen failed: 0x%08x\n", openResult);
        CFRelease(manager);
        return 1;
    }

    CFSetRef devices = IOHIDManagerCopyDevices(manager);
    if (devices == NULL || CFSetGetCount(devices) == 0)
    {
        fprintf(stderr, "No Switch 2 Pro Controller HID devices found.\n");
        if (devices != NULL)
            CFRelease(devices);
        CFRelease(manager);
        return 2;
    }

    CFIndex deviceCount = CFSetGetCount(devices);
    IOHIDDeviceRef *deviceRefs = calloc((size_t)deviceCount, sizeof(IOHIDDeviceRef));
    DeviceContext *contexts = calloc((size_t)deviceCount, sizeof(DeviceContext));

    if (deviceRefs == NULL || contexts == NULL)
    {
        fprintf(stderr, "Out of memory.\n");
        free(deviceRefs);
        free(contexts);
        CFRelease(devices);
        CFRelease(manager);
        return 1;
    }

    CFSetGetValues(devices, (const void **)deviceRefs);

    printf("Found %ld matching HID device(s).\n", (long)deviceCount);

    for (CFIndex i = 0; i < deviceCount; i++)
    {
        IOHIDDeviceRef device = deviceRefs[i];
        contexts[i].device = device;

        printf("\nDevice %ld\n", (long)(i + 1));
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

        IOHIDDeviceRegisterInputValueCallback(device, ValueCallback, &contexts[i]);
        IOHIDDeviceScheduleWithRunLoop(device, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
    }

    printf("\nListening. Press buttons/move sticks. Press Ctrl-C to stop.\n");
    fflush(stdout);

    while (!shouldStop)
    {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.5, false);
    }

    printf("\nStopping.\n");

    for (CFIndex i = 0; i < deviceCount; i++)
    {
        IOHIDDeviceUnscheduleFromRunLoop(deviceRefs[i], CFRunLoopGetMain(), kCFRunLoopDefaultMode);
        IOHIDDeviceClose(deviceRefs[i], kIOHIDOptionsTypeNone);
    }

    free(deviceRefs);
    free(contexts);
    CFRelease(devices);
    CFRelease(manager);
    return 0;
}
