// Device controller setup follows Keystone's MH1903 USB board driver.
#include <string.h>
#include "camera.h"
#include "device_diagnostics.h"
#include "usb_bsp.h"
#include "usb_dcd_int.h"
#include "usbd_core.h"
#include "usbd_ioreq.h"

static USB_OTG_CORE_HANDLE device;
#ifdef FORGEBOX_USB_SERIAL
#include "specter_cdc.inc"
#else
#define DEBUG_EP 0x81
#define REPORT_BYTES 32
#define HARDWARE_FIRST_ID 2
#define HARDWARE_CHUNKS 7
_Static_assert(HARDWARE_REPORT_SIZE == HARDWARE_CHUNKS * (REPORT_BYTES - 1), "hardware report size");
static uint8_t reply[REPORT_BYTES] __attribute__((aligned(4)));
static uint8_t idle_rate;
_Static_assert(sizeof(CameraDiagnostic) == 24, "USB report layout changed");

// pid.codes 1209:0001 is for private testing and is not globally unique.
static uint8_t device_descriptor[] = {
    18, 1, 0, 2, 0, 0, 0, 64, 0x09, 0x12, 0x01, 0x00, 0x02, 0x01, 0, 2, 0, 1,
};
#define FEATURE(id, usage) 0x85, (id), 0x09, (usage), 0xb1, 0x02
static uint8_t report_descriptor[] = {
    0x06, 0x31, 0xff, // vendor usage page FF31
    0x09, 0x01, 0xa1, 0x01,
    0x26, 0xff, 0x00, 0x75, 0x08,
    0x95, REPORT_BYTES - 1,
    FEATURE(1, 2), FEATURE(2, 3), FEATURE(3, 4), FEATURE(4, 5),
    FEATURE(5, 6), FEATURE(6, 7), FEATURE(7, 8), FEATURE(8, 9),
    0xc0,
};
_Static_assert(sizeof(report_descriptor) < 64, "HID descriptor must fit in one control packet");
static uint8_t configuration[] = {
    9, 2, 34, 0, 1, 1, 0, 0xc0, 50,
    9, 4, 0, 0, 1, 3, 0, 0, 0,
    9, 0x21, 0x11, 0x01, 0, 1, 0x22, sizeof(report_descriptor), 0,
    7, 5, DEBUG_EP, 3, REPORT_BYTES, 0, 16,
};
static uint8_t language[] = {4, 3, 0x09, 0x04};
static uint8_t product[] = {
    32, 3, 'F', 0, 'o', 0, 'r', 0, 'g', 0, 'e', 0, 'B', 0, 'o', 0, 'x', 0,
    ' ', 0, 'Q', 0, 'R', 0, ' ', 0, 'D', 0, 'b', 0, 'g', 0,
};

static uint8_t *get_device(uint8_t speed, uint16_t *length) {
    *length = sizeof(device_descriptor);
    return device_descriptor;
}
static uint8_t *get_configuration(uint8_t speed, uint16_t *length) {
    *length = sizeof(configuration);
    return configuration;
}
static uint8_t *get_language(uint8_t speed, uint16_t *length) {
    *length = sizeof(language);
    return language;
}
static uint8_t *get_product(uint8_t speed, uint16_t *length) {
    *length = sizeof(product);
    return product;
}
static uint8_t *unsupported(uint8_t speed, uint16_t *length) {
    *length = 0;
    return NULL;
}
static uint8_t *unsupported_os(uint16_t *length) {
    return unsupported(0, length);
}

static uint8_t init_class(void *handle, uint8_t configuration_index) {
    idle_rate = 0;
    return DCD_EP_Open(handle, DEBUG_EP, REPORT_BYTES, USB_OTG_EP_INT);
}
static uint8_t deinit_class(void *handle, uint8_t configuration_index) {
    return DCD_EP_Close(handle, DEBUG_EP);
}
static uint8_t send(void *handle, const USB_SETUP_REQ *request, uint8_t *data, uint16_t length) {
    USBD_CtlSendData(handle, data, length < request->wLength ? length : request->wLength);
    return USBD_OK;
}
static uint8_t setup_class(void *handle, USB_SETUP_REQ *request) {
    if (request->wIndex != 0) return USBD_FAIL;
    if (request->bmRequest == 0x81 && request->bRequest == 6) {
        if (request->wValue == 0x2200)
            return send(handle, request, report_descriptor, sizeof(report_descriptor));
        if (request->wValue == 0x2100)
            return send(handle, request, configuration + 18, 9);
    }
    if (request->bmRequest == 0xa1 && request->bRequest == 1 && request->wValue == 0x0301) {
#if defined(FORGEBOX_BOOT_PROBE)
        memset(reply, 0, sizeof(reply));
        reply[0] = 1;
        memcpy(reply + 1, "FBB1", 4);
#elif defined(FORGEBOX_SE_PAIRING)
        memset(reply, 0, sizeof(reply));
        reply[0] = 1;
        memcpy(reply + 1, "FBP1", 4);
#else
        CameraDiagnostic snapshot;
        board_camera_diagnostic(&snapshot);
        reply[0] = 1;
        memcpy(reply + 1, "FBD1", 4);
        memcpy(reply + 5, &snapshot, sizeof(snapshot));
#endif
        return send(handle, request, reply, sizeof(reply));
    }
    if (request->bmRequest == 0xa1 && request->bRequest == 1 &&
        request->wValue >= 0x0300 + HARDWARE_FIRST_ID &&
        request->wValue < 0x0300 + HARDWARE_FIRST_ID + HARDWARE_CHUNKS) {
        unsigned id = request->wValue & 0xff;
        reply[0] = id;
#ifdef FORGEBOX_BOOT_PROBE
        board_read_boot_probe((id - HARDWARE_FIRST_ID) * (REPORT_BYTES - 1),
#else
        board_read_hardware_report((id - HARDWARE_FIRST_ID) * (REPORT_BYTES - 1),
#endif
                                   reply + 1, REPORT_BYTES - 1);
        return send(handle, request, reply, sizeof(reply));
    }
    if (request->bmRequest == 0x21 && request->bRequest == 0x0a && request->wLength == 0) {
        idle_rate = request->wValue >> 8;
        USBD_CtlSendStatus(handle);
        return USBD_OK;
    }
    if (request->bmRequest == 0xa1 && request->bRequest == 0x02) {
        reply[0] = idle_rate;
        return send(handle, request, reply, 1);
    }
    if (request->bmRequest == 0x81 && request->bRequest == 0x0a) {
        reply[0] = 0;
        return send(handle, request, reply, 1);
    }
    return USBD_FAIL;
}

static void noop(void) {}
static void reset_callback(uint8_t speed) {}
static USBD_DEVICE descriptors = {
    .GetDeviceDescriptor = get_device,
    .GetLangIDStrDescriptor = get_language,
    .GetManufacturerStrDescriptor = unsupported,
    .GetProductStrDescriptor = get_product,
    .GetSerialStrDescriptor = unsupported,
    .GetConfigurationStrDescriptor = unsupported,
    .GetInterfaceStrDescriptor = unsupported,
    .GetQualiferDescriptor = unsupported,
    .GetWinUSBOSStrDescriptor = unsupported_os,
    .GetWinUSBOSFeatureDescriptor = unsupported_os,
    .GetWinUSBOSPropertyDescriptor = unsupported_os,
};
static USBD_Class_cb_TypeDef usb_class = {
    .Init = init_class,
    .DeInit = deinit_class,
    .Setup = setup_class,
    .GetConfigDescriptor = get_configuration,
};
#endif
static USBD_Usr_cb_TypeDef callbacks = {
    .Init = noop,
    .DeviceReset = reset_callback,
    .DeviceConfigured = noop,
    .DeviceSuspended = noop,
    .DeviceResumed = noop,
    .DeviceConnected = noop,
    .DeviceDisconnected = noop,
};

void USB_OTG_BSP_Init(USB_OTG_CORE_HANDLE *handle) {}
void USB_OTG_BSP_EnableInterrupt(USB_OTG_CORE_HANDLE *handle) {}
void USB_OTG_BSP_uDelay(uint32_t us) {
    for (volatile uint32_t n = 0; n < 120 * us / 7; n++) {}
}
void USB_OTG_BSP_mDelay(uint32_t ms) { USB_OTG_BSP_uDelay(ms * 1000); }

void debug_usb_init(void) {
    SYSCTRL_AHBPeriphClockCmd(SYSCTRL_AHBPeriph_USB, ENABLE);
    SYSCTRL_AHBPeriphResetCmd(SYSCTRL_AHBPeriph_USB, ENABLE);
    USBPHY_CR1_TypeDef phy = {.d32 = SYSCTRL->USBPHY_CR1};
    phy.b.commononn = 0;
    phy.b.stop_ck_for_suspend = 0;
    SYSCTRL->USBPHY_CR1 = phy.d32;
    // The SDK touches these registers before DCD_Init selects the core.
    USB_OTG_SelectCore(&device, USB_OTG_FS_CORE_ID);
    uint32_t irq_mask = __get_PRIMASK();
    __disable_irq();
    USBD_Init(&device, USB_OTG_FS_CORE_ID, &descriptors, &usb_class, &callbacks);
#ifdef FORGEBOX_USB_SERIAL
    DCD_DevDisconnect(&device);
#endif
    // USBD_Init changes the grouping, which the RTOS and camera IRQ depend on.
    NVIC_SetPriorityGrouping(NVIC_PriorityGroup_3);
    NVIC_SetPriority(USB_IRQn, NVIC_EncodePriority(NVIC_PriorityGroup_3, 3, 0));
    __set_PRIMASK(irq_mask);
}

void USB_IRQHandler(void) { USBD_OTG_ISR_Handler(&device); }
