#include <stc8h.h>

#include "hardware-definition.h"
#include "uart.h"

/* Reload value from datasheet ch.10.5:  65536 - (Fosc/Baud + 2)/4
 * 6MHz/9600: 65536 - (625+2)/4 = 65536 - 156 = 65380 = 0xFF64
 * (actual baud 9615, +0.16%)                                       */
#define T1_RELOAD  (65536UL - ((MAIN_FOSC_HZ / UART_BAUDRATE + 2UL) / 4UL))

void UART1_Init(void)
{
    /* P3.0/P3.1 quasi-bidirectional (reset default; masked to keep
     * unrelated bits of the shared mode registers untouched) */
    P3M0 &= ~0x03;
    P3M1 &= ~0x03;

    AUXR &= ~0x01;                  /* S1ST2=0: UART1 baud from Timer1 */
    SCON   = 0x50;                  /* mode1: 8-bit UART, REN=1        */
    TMOD  &= 0x0F;                  /* T1 mode0: 16-bit auto-reload    */
    TL1    = (unsigned char)(T1_RELOAD);
    TH1    = (unsigned char)(T1_RELOAD >> 8);
    AUXR  |= 0x40;                  /* T1x12=1: Timer1 in 1T mode      */
    ET1    = 0;                     /* no Timer1 interrupt             */
    TR1    = 1;                     /* start Timer1                    */
    ES     = 0;                     /* polling TX, no UART interrupt   */
}

void UART1_SendByte(unsigned char dat)
{
    TI   = 0;
    SBUF = dat;
    while (!TI);
}
