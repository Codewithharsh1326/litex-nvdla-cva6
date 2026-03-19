/* link_ram.ld — Firmware loaded by LiteX BIOS via serial (litex_term)
 *
 * When loaded via litex_term, the BIOS places the binary at main_ram
 * (0x40000000) and jumps to it. Code + data + bss all live in SRAM.
 * Stack is at the top of the 1MB main_ram region.
 *
 * Do NOT use this for ROM bake-in (--firmware-init). Use link.ld for that.
 */

ENTRY(_start)

MEMORY {
    SRAM (rwx) : ORIGIN = 0x40000000, LENGTH = 256K   /* lower 256KB for code+data */
}

SECTIONS {
    .text 0x40000000 : {
        KEEP(*(.text.start))
        *(.text*)
        *(.rodata*)
        . = ALIGN(8);
        _text_end = .;
    } > SRAM

    .data : {
        _data_start     = .;
        _data_rom_start = .;  /* LMA == VMA: BIOS loads ELF+data in one shot */
        *(.data*)
        . = ALIGN(8);
        _data_end = .;
    } > SRAM

    .bss (NOLOAD) : {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        _bss_end = .;
    } > SRAM

    /* Stack: top of 1MB main_ram */
    _stack_top = 0x40100000;

    /DISCARD/ : { *(.comment) *(.note*) }
}
