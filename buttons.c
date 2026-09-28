#include <stc8h.h>

#include "hardware-definition.h"
#include "buttons.h"

/* 7 keys, active low (external pull-up + internal pull-up enabled):
 * KEY_MAIN=P3.2, KEY1..KEY5=P3.3..P3.7, KEY6=P1.0. */

#define BUTTONS_P3_MASK  0xFC    /* P3.2..P3.7, excludes UART P3.0/P3.1 */
#define BUTTONS_P1_MASK  0x01    /* P1.0 = KEY6                          */

void Buttons_Init(void)
{
    P_SW2 |= 0x80;                /* PxPU registers are XFR-mapped       */

    P3M0 &= ~BUTTONS_P3_MASK;     /* key pins high-Z input (masked)      */
    P3M1 |= BUTTONS_P3_MASK;
    P3PU |= BUTTONS_P3_MASK;      /* internal weak pull-ups              */
    P3   |= BUTTONS_P3_MASK;      /* keep write latches high             */

    P1M0 &= ~BUTTONS_P1_MASK;     /* P1.0 same treatment (masked, keeps
                                     P1.1 VLCD / P1.2,5,6,7 HT1621)   */
    P1M1 |= BUTTONS_P1_MASK;
    P1PU |= BUTTONS_P1_MASK;
    P1   |= BUTTONS_P1_MASK;
}

/* returns a bitmask of currently PRESSED keys (1 = pressed):
 * bit0=KEY_MAIN, bit1..5=KEY1..KEY5, bit6=KEY6, bit7 spare */
unsigned char Buttons_Read(void)
{
    unsigned char s;

    s = (unsigned char)((~P3 & BUTTONS_P3_MASK) >> 2);  /* P3.2..7 -> bits 0..5 */
    if ((P1 & BUTTONS_P1_MASK) == 0)
        s |= BUTTON_KEY6;
    return s;
}
