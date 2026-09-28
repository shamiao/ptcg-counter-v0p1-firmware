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
 * ptcg-counter-v0p1 application firmware.
 *
 * Boot: capture the factory BGV mirror, bring up the UART, run the
 * overvoltage gate, init the remaining peripherals, apply the one-shot
 * VLCD policy, and bring up the HT1621 system oscillator. The debug
 * bring-up firmware this replaces is archived at dev-notes/main-debug-c.txt.
 */

/* ------------- saturating counter (core business logic) --------------
 * A plain 0..990 counter in a u16, stepped by the keys: 1=+100,
 * 2/3=+10, 4=-100, 5/6=-10; it clamps at both ends (no wrap). The
 * value is kept in plain units - NOT decades - so a future config-
 * urable step size (100/10/1) drops in without re-representation.
 * Digits are recomputed from the value on every render, so cross-
 * digit carries (90->100 and back) fall out by construction; leading
 * zeros are blanked, zero shows as "  0".                             */
static unsigned int data count_val = 0;

#define COUNTER_MAX  990u

static void Counter_ApplyKey(unsigned char key)
{
    switch (key)
    {
    case BUTTON_KEY1:                        /* +100                  */
        if (count_val > COUNTER_MAX - 100u) count_val = COUNTER_MAX;
        else                                count_val += 100;
        break;
    case BUTTON_KEY2:                        /* +10                   */
    case BUTTON_KEY3:
        if (count_val > COUNTER_MAX - 10u) count_val = COUNTER_MAX;
        else                               count_val += 10;
        break;
    case BUTTON_KEY4:                        /* -100                  */
        if (count_val < 100u) count_val = 0;
        else                  count_val -= 100;
        break;
    case BUTTON_KEY5:                        /* -10                   */
    case BUTTON_KEY6:
        if (count_val < 10u) count_val = 0;
        else                 count_val -= 10;
        break;
    default:                                 /* KEY0: action TBD      */
        break;
    }
}

static void Counter_Render(void)
{
    unsigned int data v = count_val;

    g_disp_buf[0] = DISP_EN | (unsigned char)(v % 10u);        /* units  */
    g_disp_buf[1] = (v >= 10u)                                /* tens   */
        ? (unsigned char)(DISP_EN | ((v / 10u) % 10u)) : 0;
    g_disp_buf[2] = (v >= 100u)                               /* hundr. */
        ? (unsigned char)(DISP_EN | (v / 100u)) : 0;
    Display_Render();
}

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

    /* self-test: "8.8.8" + 2kHz buzzer for 0.8s, then blank + mute for
       0.5s (LCD stays on; the beep also proves the TONE chain live) */
    g_disp_buf[0] = DISP_EN | 8;
    g_disp_buf[1] = DISP_EN | DISP_DP | 8;
    g_disp_buf[2] = DISP_EN | DISP_DP | 8;
    Display_Render();
    HT1621_Buzzer2kOn();

    {   /* boot report: the WKT calibration actually in use */
        unsigned int data fwt_rep = g_fwt_hz;
        unsigned int data cnt_rep = g_wkt_reload + 1u;
        printf("wkt fwt=%u cnt=%u\r\n", fwt_rep, cnt_rep);
    }

    DelayMs(800);                /* 0.8s of "8.8.8" + beep; drains UART */
    g_disp_buf[0] = g_disp_buf[1] = g_disp_buf[2] = 0;
    Display_Render();            /* end of all-on: dark panel           */
    HT1621_BuzzerOff();          /* mute as the panel blanks            */

    DelayMs(500);                /* 0.5s of blank + silence             */

    Timeslice_Init();            /* WKT: 20ms slices start here */

    Counter_Render();            /* counter idle display: "  0"        */

    /* Frame loop: step the key FSM once per slice; an accepted press
       applies its HP step (saturated) and re-renders the counter.
       KEY0 press/release events arrive but carry no business action
       yet. The 1s heartbeat print stays as the slice-cadence check;
       all prints are dev-phase only (see firmware-conventions). */
    {
        unsigned int data n = 0;         /* slices completed           */
        unsigned char data div50 = 0;

        while (1)
        {
            unsigned char data ev = Buttons_Scan();
            if (ev)
            {
                unsigned char data idx = 0;
                Counter_ApplyKey(ev);    /* clamped +/- step           */
                Counter_Render();
                while (ev > 1)           /* mask -> key number         */
                {
                    ev >>= 1;
                    idx++;
                }

                {
                    unsigned int data v = count_val;
                    printf("k%bu v%u\r\n", idx, v);
                }
                DelayMs(1);              /* stop bit must get out
                                            before STOP kills UART   */
            }

            if (Buttons_Key0Released())
            {
                printf("k0u\r\n");       /* KEY0 release: action TBD   */
                DelayMs(1);              /* stop bit must get out
                                            before STOP kills UART   */
            }

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
