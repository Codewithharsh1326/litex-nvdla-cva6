/* uart.c — Polled UART TX/RX for LiteX UART (CSR-based) */
#include "uart.h"

void uart_init(void) {
    /* LiteX UART is reset by hardware, 115200 baud configured in SoC */
    UART_EV_ENABLE = 0;   /* disable interrupts, we poll */
    
    /* Small delay to let hardware stabilize */
    for(volatile int i=0; i<100000; i++);
}

void uart_putc(char c) {
    /* Translate LF → CR+LF for compatibility with all serial terminals */
    if (c == '\n') {
        while (UART_TXFULL) {}
        UART_RXTX = '\r';
    }
    while (UART_TXFULL) {}
    UART_RXTX = (uint32_t)(unsigned char)c;
}

void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

void uart_put_hex32(uint32_t v) {
    static const char hex[] = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 28; i >= 0; i -= 4)
        uart_putc(hex[(v >> i) & 0xF]);
}

void uart_put_dec(int32_t v) {
    if (v < 0) { uart_putc('-'); v = -v; }
    if (v == 0) { uart_putc('0'); return; }
    char buf[12];
    int idx = 0;
    while (v > 0) {
        buf[idx++] = '0' + (v % 10);
        v /= 10;
    }
    for (int i = idx - 1; i >= 0; i--)
        uart_putc(buf[i]);
}
