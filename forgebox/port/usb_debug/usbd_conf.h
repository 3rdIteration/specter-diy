#pragma once
#include "usb_conf.h"
#define USBD_CFG_MAX_NUM 1
#ifdef FORGEBOX_USB_SERIAL
#define USBD_ITF_MAX_NUM 2
#else
#define USBD_ITF_MAX_NUM 1
#endif
#define USB_MAX_STR_DESC_SIZ 64
#define USBD_SELF_POWERED
