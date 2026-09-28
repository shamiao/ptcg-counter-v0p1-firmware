#include <stc8h.h>
#include <stdio.h>

#include "hardware-definition.h"
#include "common.h"
#include "uart.h"
#include "vlcd.h"
#include "ht1621.h"
#include "buttons.h"
#include "timeslice.h"

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
    Timeslice_EarlyInit();       /* capture the idata WKT calibration
                                    mirror right behind it            */

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

    {   /* boot report: the WKT calibration actually in use */
        unsigned int data fwt_rep = g_fwt_hz;
        unsigned int data cnt_rep = g_wkt_reload + 1u;   /* ticks/slice */
        printf("wkt fwt=%u cnt=%u\r\n", fwt_rep, cnt_rep);
        DelayMs(1);              /* stop bit must get out before STOP   */
    }

    Timeslice_Init();            /* WKT: 20ms slices start here */

    /* Demo of the slice loop: one UART line per 50 slices (~1s) proves
       the STOP/WKT cadence on a terminal. Line is ~10 chars at 9600
       baud = ~10ms of the 20ms slice - keep printf out of the release
       build (see dev-notes/firmware-conventions.md). */
    {
        unsigned int data n = 0;         /* slices completed        */
        unsigned char data div50 = 0;

        while (1)
        {
            n++;
            if (++div50 >= 50)
            {
                div50 = 0;
                printf("ts %u\r\n", n);
                DelayMs(1);              /* stop bit must get out
                                            before STOP kills UART   */
            }

            Slice_SleepOneTick();
        }
    }
}
