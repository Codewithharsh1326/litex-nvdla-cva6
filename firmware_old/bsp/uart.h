/* uart.h — Polled UART driver for LiteX UART @ 0xF0002000 */
#ifndef UART_H
#define UART_H

#include <stdint.h>

/* LiteX UART CSR offsets (32-bit word-addressed) */
/* NOTE: address comes from csr.csv — changes if SoC peripherals are added/removed */
#define UART_BASE       0x80002000UL
#define UART_RXTX       (*(volatile uint32_t *)(UART_BASE + 0x00))
#define UART_TXFULL     (*(volatile uint32_t *)(UART_BASE + 0x04))
#define UART_RXEMPTY    (*(volatile uint32_t *)(UART_BASE + 0x08))
#define UART_EV_STATUS  (*(volatile uint32_t *)(UART_BASE + 0x0C))
#define UART_EV_PENDING (*(volatile uint32_t *)(UART_BASE + 0x10))
#define UART_EV_ENABLE  (*(volatile uint32_t *)(UART_BASE + 0x14))

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_put_hex32(uint32_t v);
void uart_put_dec(int32_t v);

#endif
