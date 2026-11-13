ENTRY(Reset_Handler)
MEMORY {
    FLASH (rx) : ORIGIN = 0x01081000, LENGTH = 0xF00000
    SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 1024K
}
_estack = ORIGIN(SRAM) + LENGTH(SRAM);
PHDRS { flash PT_LOAD FLAGS(5); ram PT_LOAD FLAGS(6); }
SECTIONS {
    .interrupts 0x01081000 : { KEEP(*(.isr_vector)) } > FLASH :flash
    .fixSegment 0x01082000 : { KEEP(*(.fixSection)) } > FLASH :flash
    .text 0x01083000 : {
        *(.text*) *(.rodata*)
        . = ALIGN(4);
    } > FLASH :flash
    .ARM.extab : { *(.ARM.extab*) } > FLASH :flash
    .ARM.exidx : {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > FLASH :flash
    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > SRAM AT> FLASH :ram
    .bss ALIGN(256) (NOLOAD) : { _sbss = .; *(.bss*) *(COMMON) . = ALIGN(8); _ebss = .; } > SRAM :ram
    end = .;
    _end = .;
    ASSERT(_ebss < _estack - 0x4000, "SRAM overlaps exception stack")
}
