/* spike_uart.c — HTIF console output for Spike (device=1, cmd=1) */

#include "platform.h"
#include "test.h"

extern volatile uint64_t tohost;
extern volatile uint64_t fromhost;

void uart_putc(char c)
{
    while (tohost != 0)
        ;
    tohost = ((uint64_t)1 << 56) | ((uint64_t)1 << 48) | (unsigned char)c;
    while (tohost != 0)
        ;
    if (fromhost != 0)
        fromhost = 0;
}
