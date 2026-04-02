/*
    GNU linker script for STM32U5G9ZJ (4 MB Flash, 3 MB SRAM)
    Used on the STM32U5G9J-DK2 Discovery Kit
*/

/* Specify the memory areas */
MEMORY
{
    FLASH (rx)      : ORIGIN = 0x08000000, LENGTH = 4096K   /* Entire flash (4 MB) */
    FLASH_ISR (rx)  : ORIGIN = 0x08000000, LENGTH = 128K    /* Sector 0 for ISR vector */
    FLASH_TEXT (rx) : ORIGIN = 0x08020000, LENGTH = 3968K   /* Remaining sectors for code */
    RAM (xrw)       : ORIGIN = 0x20000000, LENGTH = 3072K   /* 3 MB SRAM (SRAM1+SRAM2+SRAM3+SRAM4) */
}

/* produce a link error if there is not this amount of RAM for these sections */
_minimum_stack_size = 2K;
_minimum_heap_size = 16K;

/* Define the stack.  The stack is full descending so begins just above last byte
   of RAM.  Note that EABI requires the stack to be 8-byte aligned for a call. */
_estack = ORIGIN(RAM) + LENGTH(RAM) - _estack_reserve;
_sstack = _estack - 32K; /* tunable - larger stack for Cortex-M33 */

/* RAM extents for the garbage collector */
_ram_start = ORIGIN(RAM);
_ram_end = ORIGIN(RAM) + LENGTH(RAM);
_heap_start = _ebss; /* heap starts just after statically allocated memory */
_heap_end = _sstack;
