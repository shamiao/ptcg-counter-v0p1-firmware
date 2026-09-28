#include <stc8h.h>
#include <stdio.h>

#include "hardware-definition.h"
#include "common.h"
#include "uart.h"
#include "vlcd.h"
#include "ht1621.h"
#include "buttons.h"
#include "timeslice.h"
#include "display.h"

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
    HT1621_LcdOn();              /* BIAS 1/3 4COM + LCD_ON            */

    /* self-test display: "8.8.8" for 1s, then blank (LCD stays on) */
    g_disp_buf[0] = DISP_EN | 8;
    g_disp_buf[1] = DISP_EN | DISP_DP | 8;
    g_disp_buf[2] = DISP_EN | DISP_DP | 8;
    Display_Render();

    {   /* boot report: the WKT calibration actually in use */
        unsigned int data fwt_rep = g_fwt_hz;
        unsigned int data cnt_rep = g_wkt_reload + 1u;
        printf("wkt fwt=%u cnt=%u\r\n", fwt_rep, cnt_rep);
    }

    DelayMs(1000);               /* 1s of "8.8.8"; also drains the UART */
    g_disp_buf[0] = g_disp_buf[1] = g_disp_buf[2] = 0;
    Display_Render();            /* end of self-test: dark panel        */

    Timeslice_Init();            /* WKT: 20ms slices start here */

    g_disp_buf[0] = DISP_EN | 0; /* business idle display: "  0"       */
    Display_Render();

    /* Slice-mechanism demo: every 50 slices (~1s) advance the charset
       test one step - the 36 glyphs cycle three at a time (012, 345,
       ... , XYZ) - and print one UART line to cross-check the cadence.
       Keep printf out of the release build (see dev-notes/
       firmware-conventions.md). */
    {
        unsigned int data n = 0;         /* slices completed           */
        unsigned char data div50 = 0;
        unsigned char data base = 0;     /* first code of the triple   */

        while (1)
        {
            n++;
            if (++div50 >= 50)
            {
                div50 = 0;
                printf("ts %u\r\n", n);
                DelayMs(1);              /* stop bit must get out
                                            before STOP kills UART   */

                g_disp_buf[2] = DISP_EN | base;        /* hundreds */
                g_disp_buf[1] = DISP_EN | (base + 1);  /* tens     */
                g_disp_buf[0] = DISP_EN | (base + 2);  /* units    */
                Display_Render();
                base += 3;
                if (base >= 36)
                    base = 0;
            }

            Slice_SleepOneTick();
        }
    }
}
