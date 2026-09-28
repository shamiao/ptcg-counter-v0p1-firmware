#include <stc8h.h>
#include <intrins.h>

#include "hardware-definition.h"
#include "common.h"
#include "uart.h"
#include "vlcd.h"

/* rough software delay, not calibrated (~1ms per unit @ MAIN_FOSC_HZ) */
void DelayMs(unsigned int ms)
{
    unsigned int i;
    while (ms--)
    {
        i = (unsigned int)(MAIN_FOSC_HZ / 6000UL);
        while (--i);
    }
}

/* >= 1us per unit @ 6MHz 1T core */
void DelayUs(unsigned int us)
{
    while (us--)
        NOP6();   /* 6 NOPs = 1us @6MHz, plus overhead */
}

/* Keil C51 printf() retarget */
char putchar(char c)
{
    if (c == '\n')
        UART1_SendByte('\r');
    UART1_SendByte((unsigned char)c);
    return c;
}

/* Fatal-error terminator, shared by every failed self-test.
 * Sequence: let the last UART byte drain, force VLCD hard low (panel
 * undriven), put every port back into plain quasi-bidirectional safe
 * states (HT1621 bus idle: CS/WR/RD high, DATA low), disable ALL
 * interrupts (EA gates every source; none was enabled anyway), then
 * power the MCU down forever. A dead loop backstops any spurious wake. */
void SleepForever(void)
{
    DelayMs(2);               /* let the last UART stop bit out */

    VLCD_PWM_Off(0);          /* P1.1 back to GPIO, driven low */

    P1M0 = 0x00; P1M1 = 0x00; /* every pin quasi-bidirectional again */
    P3M0 = 0x00; P3M1 = 0x00;
    P5M0 = 0x00; P5M1 = 0x00;
    P1  = 0x7D;               /* P1.1=0 (VLCD), CS/WR/RD=1 (idle),
                                 P1.7=0 (DATA low), P1.0=1 (key)  */
    P3  = 0xFF;               /* keys + UART idle high             */
    P5  = 0xFF;

    IE  = 0x00;               /* every interrupt off, EA included  */
    PCON |= 0x02;             /* PD: permanent power-down           */
    while (1);                /* dead-loop backstop                 */
}
