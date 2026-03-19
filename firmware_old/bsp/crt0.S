/* crt0.S — Bare-metal startup for CVA6 RV64IMAC
 *
 * ROM:  0x10000000 — code runs here
 * SRAM: 0x40000000 — data/bss/stack here
 */
    .section .text.start
    .global _start
_start:
    /* Set stack pointer to top of SRAM */
    la   sp, _stack_top

    /* Copy .data from ROM (LMA = _data_rom_start) to SRAM (VMA = _data_start) */
    la   a0, _data_start
    la   a1, _data_rom_start
    la   a2, _data_end
    beq  a0, a1, .Lbss_clear   /* already in place (shouldn't happen) */
    beq  a0, a2, .Lbss_clear   /* empty .data section */
.Ldata_copy:
    lw   t0, 0(a1)
    sw   t0, 0(a0)
    addi a0, a0, 4
    addi a1, a1, 4
    blt  a0, a2, .Ldata_copy

.Lbss_clear:
    /* Zero .bss */
    la   a0, _bss_start
    la   a1, _bss_end
    beq  a0, a1, .Lmain_call
.Lbss_loop:
    sw   zero, 0(a0)
    addi a0, a0, 4
    blt  a0, a1, .Lbss_loop

.Lmain_call:
    /* Jump to C main */
    call main

.Linf_loop:
    j .Linf_loop              /* hang if main returns */
