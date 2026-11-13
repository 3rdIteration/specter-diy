// CDC ACM transport. A single OUT packet is retained until read by the VM.
enum { DATA_IN = 0x81, DATA_OUT = 0x01, NOTIFY_IN = 0x82, PACKET = 64 };
static _Alignas(4) uint8_t rx[PACKET], tx[PACKET];
static _Alignas(4) uint8_t line[8] = {0x80, 0x25, 0, 0, 0, 0, 8};
static volatile unsigned received;
static volatile bool configured, dtr, sending;
static bool attached;

// SpecterDIY serial identity expected by async-hwi discovery.
static uint8_t device_descriptor[] = {
    18, 1, 0, 2, 2, 0, 0, 64, 0x55, 0xf0, 0x02, 0x98, 0x01, 0x01, 0, 2, 0, 1,
};
static uint8_t configuration[] = {
    9, 2, 67, 0, 2, 1, 0, 0xc0, 50,
    9, 4, 0, 0, 1, 2, 2, 1, 0,
    5, 0x24, 0, 0x10, 0x01,
    5, 0x24, 1, 0, 1,
    4, 0x24, 2, 2,
    5, 0x24, 6, 0, 1,
    7, 5, NOTIFY_IN, 3, 8, 0, 16,
    9, 4, 1, 0, 2, 0x0a, 0, 0, 0,
    7, 5, DATA_OUT, 2, PACKET, 0, 0,
    7, 5, DATA_IN, 2, PACKET, 0, 0,
};
_Static_assert(sizeof(configuration) == 67, "CDC descriptor length");
static uint8_t language[] = {4, 3, 0x09, 0x04};
static uint8_t product[] = {38, 3, 'S',0,'p',0,'e',0,'c',0,'t',0,'e',0,'r',0,' ',0,
                            '(',0,'F',0,'o',0,'r',0,'g',0,'e',0,'B',0,'o',0,'x',0,')',0};
_Static_assert(sizeof(product) == 38, "USB product string length");
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
static uint8_t *unsupported_os(uint16_t *length) { return unsupported(0, length); }
static void clear_session(void) {
    configured = dtr = sending = false;
    received = 0;
}
static uint8_t cdc_init(void *handle, uint8_t index) {
    clear_session();
    DCD_EP_Open(handle, DATA_IN, PACKET, USB_OTG_EP_BULK);
    DCD_EP_Open(handle, DATA_OUT, PACKET, USB_OTG_EP_BULK);
    DCD_EP_Open(handle, NOTIFY_IN, 8, USB_OTG_EP_INT);
    DCD_EP_PrepareRx(handle, DATA_OUT, rx, 0);
    configured = true;
    return USBD_OK;
}
static uint8_t cdc_deinit(void *handle, uint8_t index) {
    clear_session();
    DCD_EP_Close(handle, DATA_IN);
    DCD_EP_Close(handle, DATA_OUT);
    DCD_EP_Close(handle, NOTIFY_IN);
    return USBD_OK;
}
static uint8_t cdc_setup(void *handle, USB_SETUP_REQ *req) {
    if (req->wIndex == 0) {
        if (req->bmRequest == 0x21 && req->bRequest == 0x20 && req->wLength == 7) {
            USBD_CtlPrepareRx(handle, line, 7);
            return USBD_OK;
        }
        if (req->bmRequest == 0xa1 && req->bRequest == 0x21 && req->wLength == 7) {
            USBD_CtlSendData(handle, line, 7);
            return USBD_OK;
        }
        if (req->bmRequest == 0x21 && req->bRequest == 0x22 && req->wLength == 0) {
            if (!dtr || !(req->wValue & 1)) {
                received = 0;
                DCD_EP_PrepareRx(handle, DATA_OUT, rx, 0);
            }
            dtr = req->wValue & 1;
            return USBD_OK;
        }
    }
    if (req->wIndex < 2 && req->bmRequest == 0x81 && req->bRequest == 10 && req->wLength == 1) {
        static uint8_t alt;
        USBD_CtlSendData(handle, &alt, 1);
        return USBD_OK;
    }
    return USBD_FAIL;
}
static uint8_t cdc_in(void *handle, uint8_t ep) {
    if (ep == (DATA_IN & 0x7f)) sending = false;
    return USBD_OK;
}
static uint8_t cdc_out(void *handle, uint8_t ep) {
    USB_OTG_CORE_HANDLE *core = handle;
    unsigned count = core->dev.out_ep[ep].xfer_count;
    if (ep == DATA_OUT && dtr && count && count <= sizeof(rx)) received = count;
    else if (ep == DATA_OUT) DCD_EP_PrepareRx(handle, DATA_OUT, rx, 0);
    return USBD_OK;
}

void board_usb_serial_enable(bool enable) {
    if (attached == enable) return;
    uint32_t irq = __get_PRIMASK();
    __disable_irq();
    attached = enable;
    if (enable) DCD_DevConnect(&device);
    else {
        DCD_DevDisconnect(&device);
        clear_session();
    }
    __set_PRIMASK(irq);
}
bool board_usb_serial_connected(void) {
    return attached && configured && dtr && device.dev.device_status == USB_OTG_CONFIGURED;
}
unsigned board_usb_serial_read(uint8_t *out) {
    uint32_t irq = __get_PRIMASK();
    __disable_irq();
    unsigned count = board_usb_serial_connected() ? received : 0;
    if (count) {
        memcpy(out, rx, count);
        received = 0;
        DCD_EP_PrepareRx(&device, DATA_OUT, rx, 0);
    }
    __set_PRIMASK(irq);
    return count;
}
bool board_usb_serial_write(const uint8_t *data, unsigned size) {
    uint32_t irq = __get_PRIMASK();
    __disable_irq();
    bool ready = board_usb_serial_connected() && !sending && size && size < PACKET;
    if (ready) {
        memcpy(tx, data, size);
        sending = true;
        DCD_EP_Tx(&device, DATA_IN, tx, size);
    }
    __set_PRIMASK(irq);
    return ready;
}

static USBD_DEVICE descriptors = {
    .GetDeviceDescriptor = get_device, .GetLangIDStrDescriptor = get_language,
    .GetManufacturerStrDescriptor = unsupported, .GetProductStrDescriptor = get_product,
    .GetSerialStrDescriptor = unsupported, .GetConfigurationStrDescriptor = unsupported,
    .GetInterfaceStrDescriptor = unsupported, .GetQualiferDescriptor = unsupported,
    .GetWinUSBOSStrDescriptor = unsupported_os, .GetWinUSBOSFeatureDescriptor = unsupported_os,
    .GetWinUSBOSPropertyDescriptor = unsupported_os,
};
static USBD_Class_cb_TypeDef usb_class = {
    .Init = cdc_init, .DeInit = cdc_deinit, .Setup = cdc_setup, .DataIn = cdc_in,
    .DataOut = cdc_out, .GetConfigDescriptor = get_configuration,
};
static void noop(void) {}
static void reset_callback(uint8_t speed) { clear_session(); }
