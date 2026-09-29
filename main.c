#include <stc8h.h>
#include <stdio.h>

#include "hardware-definition.h"
#include "common.h"
#include "uart.h"
#include "vlcd.h"
#include "ht1621.h"
#include "buttons.h"
#include "display.h"
#include "timeslice.h"
#include "app.h"

/*
 * ptcg-counter-v0p1 application firmware.
 *
 * Boot: capture the factory BGV mirror, bring up the UART, run the
 * overvoltage gate, init the remaining peripherals, apply the one-shot
 * VLCD policy, run the self-test, then hand control to the app mode
 * dispatcher. The frame loop below only collects key events once per
 * 20ms slice and feeds them to App_Slice(); all UI logic lives in the
 * mode modules and is strictly non-blocking. The debug bring-up
 * firmware this replaces is archived at dev-notes/main-debug-c.txt.
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

    /* self-test: "8.8.8" for 0.5s, then blank 0.3s (LCD stays on).
       No buzzer in the self-test any more - every mode entry (the
       count entry right below included) chirps the 2kHz tone, so the
       TONE chain is still proven at every power-up, just later. */
    g_disp_buf[0] = DISP_EN | 8;
    g_disp_buf[1] = DISP_EN | DISP_DP | 8;
    g_disp_buf[2] = DISP_EN | DISP_DP | 8;
    Display_Render();

    {   /* boot report: the WKT calibration actually in use */
        unsigned int data fwt_rep = g_fwt_hz;
        unsigned int data cnt_rep = g_wkt_reload + 1u;
        printf("wkt fwt=%u cnt=%u\r\n", fwt_rep, cnt_rep);
    }

    DelayMs(500);                /* 0.5s of "8.8.8"; drains the UART   */
    g_disp_buf[0] = g_disp_buf[1] = g_disp_buf[2] = 0;
    Display_Render();            /* end of all-on: dark panel           */

    DelayMs(300);                /* 0.3s of blank                       */

    Timeslice_Init();            /* WKT: 20ms slices start here */

    App_BootViaStandby();        /* boot traverses standby: voltage
                                     gates, hardware resume, (low-batt
                                     warning if due), then the count
                                     mode entry chirps                  */

    /* Frame loop: collect the key events of this slice, hand them to
       the mode dispatcher, print the 5s heartbeat as the slice-cadence
       check, sleep to the next WKT wake. All prints are dev-phase only
       (see dev-notes/firmware-conventions.md). */
    {
        unsigned int data n = 0;         /* slices completed           */
        unsigned char data div250 = 0;

        while (1)
        {
            unsigned char data ev = Buttons_Scan();
            if (Buttons_Key0Released())
                ev |= KEY_EV_K0UP;        /* bundle the release event  */

            App_Slice(ev);

            n++;
            if (++div250 >= 250)          /* 250 slices = 5s           */
            {
                div250 = 0;
                printf("ts %u\r\n", n);
                DelayMs(1);              /* stop bit must get out
                                            before STOP kills UART   */
            }

            Slice_SleepOneTick();
        }
    }
}
