#include <stc8h.h>

#include "hardware-definition.h"
#include "common.h"
#include "uart.h"
#include "vlcd.h"
#include "ht1621.h"
#include "buttons.h"

/*
 * ptcg-counter-v0p1 application firmware (business logic TBD).
 *
 * Boot: capture the factory BGV mirror, bring up the UART, run the
 * overvoltage gate, init the remaining peripherals, apply the one-shot
 * VLCD policy, and bring up the HT1621 system oscillator. Everything
 * below that is up to the business logic. The debug bring-up firmware
 * this replaces is archived at dev-notes/main-debug-c.txt.
 */

void main(void)
{
    unsigned int  vcc_mv;

    VLCD_EarlyInit();            /* FIRST: capture the idata factory
                                    BGV mirror before anything else */

    P5M0 &= ~0x10;               /* P5.4 (NC) -> high-Z input (masked) */
    P5M1 |= 0x10;

    UART1_Init();

    /* Overvoltage gate: THE FIRST functional check, run as early as it
       is reportable - right after the UART is up. VCC is measured
       once; at/above the limit the machine halts forever via the
       shared fatal terminator. */
    vcc_mv = VCC_MeasureMv();
    if (vcc_mv >= VCC_OVERVOLT_MV)
        SleepForever();

    Buttons_Init();
    HT1621_Init();

    VLCD_SetByVccMv(vcc_mv);     /* one-shot VLCD bias for this VCC */

    HT1621_SysInit();            /* SYS_EN + RC_256K: LCD / TONE base */

    /* Business logic goes here: segment display, key scanning and
       debounce, counting state machine, low-power idle. */
    while (1)
    {
    }
}
