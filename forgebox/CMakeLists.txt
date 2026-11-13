cmake_minimum_required(VERSION 3.16)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
project(specter_forgebox C ASM)
option(FORGEBOX_PRODUCTION "Enable versioned production wallet storage" OFF)
set(FORGEBOX_VERSION "1.10.5-forgebox" CACHE STRING "Application version")
add_compile_definitions(FORGEBOX_VERSION="${FORGEBOX_VERSION}")
if(FORGEBOX_PRODUCTION)
    add_compile_definitions(FORGEBOX_PRODUCTION=1)
endif()
option(FORGEBOX_USB_DEBUG "Build the private USB HID camera diagnostic interface" OFF)
option(FORGEBOX_USB_SERIAL "Build the Specter USB CDC wallet transport" OFF)
option(FORGEBOX_BOOT_EXPORT "Enable on-device boot flash export to microSD" OFF)
option(FORGEBOX_BOOT_PROBE "Build the standalone boot ROM and fingerprint probe" OFF)
option(FORGEBOX_FP_ENROLL_TEST "Enable the standalone fingerprint enrollment test" OFF)
option(FORGEBOX_FP_UNLOCK_TEST "Enable the RAM-only Gen 1 fingerprint unlock test" OFF)
option(FORGEBOX_FP_UNLOCK "Enable persistent Gen 1 fingerprint unlock" OFF)
option(FORGEBOX_SE_PAIRING "Run standalone secure-element pairing diagnostics" OFF)
option(FORGEBOX_SE_STORAGE_WRITE "Enable the confirmed DS28S60 page 5 round-trip test" OFF)
option(FORGEBOX_SE_PIN_TEST "Enable the disposable page 5 PIN retry and SPI replay test" OFF)
option(FORGEBOX_SE_RESET_TEST "Enable the disposable two-page wallet reset test" OFF)
option(FORGEBOX_SE_COMBINED_TEST "Enable disposable Gen 1 account provisioning and combined unlock" OFF)
set(GEN1_STORAGE_DEFAULT ON)
if(FORGEBOX_USB_DEBUG OR FORGEBOX_SE_PAIRING OR FORGEBOX_BOOT_PROBE)
    set(GEN1_STORAGE_DEFAULT OFF)
endif()
option(FORGEBOX_GEN1_STORAGE "Build the Gen 1 Specter storage backend" ${GEN1_STORAGE_DEFAULT})
set(FB ${CMAKE_CURRENT_SOURCE_DIR}/vendor/forgebox)
set(DISCO ${CMAKE_CURRENT_SOURCE_DIR}/vendor/specter/f469-disco)
set(GRAPHICS ${DISCO}/usermods/udisplay_f469)
set(MCU -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16)
add_compile_options(${MCU} -Os -ffunction-sections -fdata-sections -Wall -Wno-format -Wno-unused-function)
add_compile_definitions(USE_STDPERIPH_DRIVER USE_FULL_ASSERT ARM_MATH_CM4 __TARGET_FPU_VFP NDEBUG)
file(GLOB MCU_DRIVERS ${FB}/external/mh1903_lib/MHSCPU_Driver/src/*.c)
file(GLOB_RECURSE LVGL ${GRAPHICS}/lvgl/src/*.c)
list(REMOVE_ITEM LVGL ${GRAPHICS}/lvgl/src/lv_core/lv_obj.c)
list(APPEND LVGL ${CMAKE_CURRENT_SOURCE_DIR}/build/lv_obj.c)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/build/lv_obj.c PROPERTIES
    INCLUDE_DIRECTORIES "${GRAPHICS}/lvgl/src/lv_core")
file(GLOB FONTS ${GRAPHICS}/fonts/*.c)
set(RTOS ${FB}/external/FreeRTOS)
set(RTOS_SRC ${RTOS}/Source/croutine.c ${RTOS}/Source/event_groups.c
    ${RTOS}/Source/list.c ${RTOS}/Source/queue.c ${RTOS}/Source/stream_buffer.c
    ${RTOS}/Source/tasks.c ${RTOS}/Source/timers.c
    ${RTOS}/Source/portable/GCC/ARM_CM4F/port.c
    ${RTOS}/Source/portable/MemMang/heap_4.c
    ${RTOS}/CMSIS_RTOS_V2/cmsis_os2.c ${RTOS}/rtos_expand.c)
set(BOARD_DRIVERS drv_sys drv_psram drv_lcd_bright drv_parallel8080
    drv_ili9806 drv_nt35510 hal_lcd drv_battery drv_i2c_io
    drv_i2c hal_touch drv_cst726 drv_ft6336 drv_gsl1691 drv_cst3xx
    hardware_version drv_power drv_spi drv_gd25qxx drv_spi_io)
set(BOARD_SRC)
foreach(DRIVER ${BOARD_DRIVERS})
    list(APPEND BOARD_SRC ${FB}/src/driver/${DRIVER}.c)
endforeach()
set(SPECTER_SPLASH ${CMAKE_CURRENT_SOURCE_DIR}/assets/specter-splash.rgb565)
file(SIZE "${SPECTER_SPLASH}" SPECTER_SPLASH_SIZE)
if(NOT SPECTER_SPLASH_SIZE EQUAL 387072)
    message(FATAL_ERROR "Expected the 384-pixel-wide RGB565 splash strips")
endif()
configure_file(assets/specter-splash.S.in specter-splash.S @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/specter-splash.S"
    PROPERTIES OBJECT_DEPENDS "${SPECTER_SPLASH}")
add_executable(specter.elf port/board.c port/vm.c port/camera.c port/camera_preview.c port/pin_flash.c
    ${CMAKE_CURRENT_BINARY_DIR}/specter-splash.S
    port/device_diagnostics.c
    ${CMAKE_CURRENT_SOURCE_DIR}/build/drv_sdcard.c
    ${CMAKE_CURRENT_SOURCE_DIR}/build/drv_sdio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/build/drv_aw32001.c
    ${BOARD_SRC} ${MCU_DRIVERS} ${RTOS_SRC} ${LVGL} ${FONTS}
    ${GRAPHICS}/pixelart/px_img.c
    ${FB}/src/ram/user_memory.c ${CMAKE_CURRENT_SOURCE_DIR}/build/psram_heap_4.c
    ${FB}/src/utils/user_delay.c ${FB}/src/utils/user_utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/build/startup.s)
target_include_directories(specter.elf PRIVATE port build/vm
    ${DISCO}/micropython ${GRAPHICS} ${GRAPHICS}/lvgl
    ${FB}/src ${FB}/src/config ${FB}/src/driver ${FB}/src/ram ${FB}/src/utils
    ${RTOS} ${RTOS}/Source/include ${RTOS}/Source/portable/GCC/ARM_CM4F
    ${RTOS}/CMSIS_RTOS_V2 ${FB}/external/mh1903_lib/MHSCPU_Driver/inc
    ${FB}/external/mh1903_lib/Device/MegaHunt/mhscpu/Include
    ${FB}/external/mh1903_lib/CMSIS/Include)
if(FORGEBOX_USB_DEBUG OR FORGEBOX_USB_SERIAL)
    if(FORGEBOX_USB_DEBUG AND FORGEBOX_USB_SERIAL)
        message(FATAL_ERROR "Choose one USB interface")
    endif()
    set(USB_SDK ${FB}/external/mh1903_lib/SCPU_USB_Lib)
    set(USB_OTG ${USB_SDK}/SCPU_USB_OTG_Driver)
    set(USB_DEV ${USB_SDK}/SCPU_USB_Device_Library/Core)
    target_compile_definitions(specter.elf PRIVATE
        $<$<BOOL:${FORGEBOX_USB_DEBUG}>:FORGEBOX_USB_DEBUG=1>
        $<$<BOOL:${FORGEBOX_USB_SERIAL}>:FORGEBOX_USB_SERIAL=1>)
    target_include_directories(specter.elf PRIVATE port/usb_debug ${USB_OTG}/inc ${USB_DEV}/inc)
    target_sources(specter.elf PRIVATE port/usb_debug/debug_usb.c
        ${USB_OTG}/src/usb_core.c ${USB_OTG}/src/usb_dcd.c ${USB_OTG}/src/usb_dcd_int.c
        ${USB_DEV}/src/usbd_core.c ${USB_DEV}/src/usbd_ioreq.c
        ${CMAKE_CURRENT_SOURCE_DIR}/build/usbd_debug_req.c)
endif()
if(FORGEBOX_BOOT_EXPORT)
    if(FORGEBOX_BOOT_PROBE OR FORGEBOX_USB_DEBUG OR FORGEBOX_USB_SERIAL)
        message(FATAL_ERROR "Boot export requires a dedicated build without USB")
    endif()
    target_compile_definitions(specter.elf PRIVATE FORGEBOX_BOOT_EXPORT=1)
    target_sources(specter.elf PRIVATE port/boot_flash_read.c)
endif()
if(FORGEBOX_BOOT_PROBE)
    if(NOT FORGEBOX_USB_DEBUG OR FORGEBOX_SE_PAIRING OR FORGEBOX_GEN1_STORAGE)
        message(FATAL_ERROR "Boot probe requires USB diagnostics without a wallet or pairing tests")
    endif()
    target_compile_definitions(specter.elf PRIVATE FORGEBOX_BOOT_PROBE=1 LV_CONF_INCLUDE_SIMPLE=1)
    target_compile_definitions(specter.elf PRIVATE MBEDTLS_CONFIG_FILE="mbedtls_config.h")
    target_include_directories(specter.elf PRIVATE ${DISCO}/usermods/uhashlib/crypto
        ${DISCO}/micropython/lib/mbedtls/include)
    target_sources(specter.elf PRIVATE port/boot_probe.c)
endif()
if(FORGEBOX_FP_ENROLL_TEST)
    if(NOT FORGEBOX_BOOT_PROBE)
        message(FATAL_ERROR "Fingerprint enrollment test requires the standalone boot probe")
    endif()
    target_compile_definitions(specter.elf PRIVATE FORGEBOX_FP_ENROLL_TEST=1)
endif()
if(FORGEBOX_FP_UNLOCK_TEST OR FORGEBOX_FP_UNLOCK)
    if(NOT FORGEBOX_GEN1_STORAGE OR FORGEBOX_USB_DEBUG)
        message(FATAL_ERROR "Fingerprint unlock test requires Gen 1 storage without USB diagnostics")
    endif()
    if(FORGEBOX_FP_UNLOCK_TEST AND FORGEBOX_FP_UNLOCK)
        message(FATAL_ERROR "Choose one fingerprint unlock build")
    endif()
    target_compile_definitions(specter.elf PRIVATE
        $<$<BOOL:${FORGEBOX_FP_UNLOCK_TEST}>:FORGEBOX_FP_UNLOCK_TEST=1>
        $<$<BOOL:${FORGEBOX_FP_UNLOCK}>:FORGEBOX_FP_UNLOCK=1>
        MBEDTLS_CONFIG_FILE="mbedtls_config.h")
    target_include_directories(specter.elf PRIVATE ${DISCO}/micropython/lib/mbedtls/include)
endif()
if(FORGEBOX_SE_PAIRING)
    if(NOT FORGEBOX_USB_DEBUG)
        message(FATAL_ERROR "Secure-element diagnostics require the USB report interface")
    endif()
    target_compile_definitions(specter.elf PRIVATE FORGEBOX_SE_PAIRING=1 LV_CONF_INCLUDE_SIMPLE=1)
    target_include_directories(specter.elf PRIVATE ${DISCO}/usermods/uhashlib/crypto)
endif()
if(FORGEBOX_GEN1_STORAGE)
    if(FORGEBOX_USB_DEBUG OR FORGEBOX_SE_PAIRING)
        message(FATAL_ERROR "Gen 1 wallet storage requires a build without diagnostics")
    endif()
    target_compile_definitions(specter.elf PRIVATE FORGEBOX_GEN1_STORAGE=1)
    target_include_directories(specter.elf PRIVATE ${DISCO}/usermods/uhashlib/crypto)
endif()
if(FORGEBOX_SE_STORAGE_WRITE)
    if(NOT FORGEBOX_SE_PAIRING)
        message(FATAL_ERROR "The storage write test requires standalone pairing diagnostics")
    endif()
    target_compile_definitions(specter.elf PRIVATE FORGEBOX_SE_STORAGE_WRITE=1)
endif()
if(FORGEBOX_SE_PIN_TEST)
    if(NOT FORGEBOX_SE_STORAGE_WRITE)
        message(FATAL_ERROR "The PIN test requires the guarded page 5 writer")
    endif()
    target_compile_definitions(specter.elf PRIVATE FORGEBOX_SE_PIN_TEST=1)
endif()
if(FORGEBOX_SE_RESET_TEST)
    if(NOT FORGEBOX_SE_PIN_TEST)
        message(FATAL_ERROR "The wallet reset test requires the disposable PIN test support")
    endif()
    target_compile_definitions(specter.elf PRIVATE FORGEBOX_SE_RESET_TEST=1)
endif()
if(FORGEBOX_SE_COMBINED_TEST)
    if(NOT FORGEBOX_SE_RESET_TEST)
        message(FATAL_ERROR "The combined test requires the disposable wallet reset test")
    endif()
    target_compile_definitions(specter.elf PRIVATE FORGEBOX_SE_COMBINED_TEST=1)
endif()
target_link_options(specter.elf PRIVATE ${MCU} -nostartfiles --specs=nano.specs --specs=nosys.specs
    -T${CMAKE_CURRENT_SOURCE_DIR}/port/mh1903.ld
    -Wl,--gc-sections,-Map=${CMAKE_BINARY_DIR}/specter.map)
target_link_libraries(specter.elf PRIVATE -Wl,--start-group
    ${CMAKE_CURRENT_SOURCE_DIR}/build/vm/libmicropython.a
    ${FB}/external/mh1903_lib/MHSCPU_Driver/lib/MH1903_QRDecodeLib.a
    m c gcc -Wl,--end-group)
add_custom_command(TARGET specter.elf POST_BUILD
    COMMAND arm-none-eabi-objcopy -O binary $<TARGET_FILE:specter.elf> ${CMAKE_BINARY_DIR}/specter.bin
    COMMAND arm-none-eabi-size $<TARGET_FILE:specter.elf>)
