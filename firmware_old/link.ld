/* link.ld — LiteX CVA6 bare-metal firmware
 *
 * Memory map:
 *   ROM  (RX ) : 0x10000000 — 16KB    code + rodata  (baked into bitstream)
 *   SRAM (RWX) : 0x40000000 — 1MB     data + bss + stack + NVDLA buffers
 *
 * The .data section is stored in ROM (LMA) and copied to SRAM (VMA) by crt0.S.
 */

ENTRY(_start)

MEMORY {
    ROM  (rx)  : ORIGIN = 0x10000000, LENGTH = 16K
    SRAM (rwx) : ORIGIN = 0x40000000, LENGTH = 1024K
}

SECTIONS {
    /* Code + read-only data → ROM */
    .text 0x10000000 : {
        KEEP(*(.text.start))    /* crt0.S _start must be first */
        *(.text*)
        *(.rodata*)
        . = ALIGN(8);
        _text_end = .;
    } > ROM

    /* Initialised data: LMA in ROM (right after .text), VMA in SRAM */
    .data 0x40000000 : AT(_text_end) {
        _data_start     = .;
        _data_rom_start = LOADADDR(.data);
        *(.data*)
        . = ALIGN(8);
        _data_end = .;
    } > SRAM

    /* Zero-init data */
    .bss (NOLOAD) : {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        _bss_end = .;
    } > SRAM

    /* Stack top = end of 1MB SRAM */
    _stack_top = ORIGIN(SRAM) + LENGTH(SRAM);

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}
