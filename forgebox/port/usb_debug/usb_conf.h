#pragma once
#include "mhscpu.h"
#define USB_OTG_MAX_EP_COUNT 8
#define USE_DEVICE_MODE
#define VBUS_SENSING_ENABLED
#define __ALIGN_BEGIN
#define __ALIGN_END
#define IS_FIFO_SIZE(size) ((size) == 0 || (size) == 8 || (size) == 16 || (size) == 32 || \
                           (size) == 64 || (size) == 128 || (size) == 256 || (size) == 512)
