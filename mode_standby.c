#include <stc8h.h>

#include "buttons.h"
#include "display.h"
#include "ht1621.h"
#include "timeslice.h"
#include "vlcd.h"
#include "app.h"
#include "mode_standby.h"

/* ------------- mode 2: standby / pre-power-on -------------------------
 * Two phases, both driven from the normal frame loop - the deep STOP
 * is the very same one in Slice_SleepOneTick(), just with the WKT
 * stopped so only INT0 can end it:
 *
 * SLEEP - WKT disabled (no slice ticks), INT0 (KEY0, falling edge)
 *         armed as the only wake. Panel parked, tone off, pins safe.
 * AWAIT - after the wake: INT0 disarmed (the firmware's no-interrupt
 *         invariant is restored), WKT re-enabled, and a 1s window
 *         runs on 20ms slices watching for the KEY0 release:
 *           release inside the window  -> accidental touch: back to
 *                                          SLEEP, window rearmed at
 *                                          the next wake;
 *           full second without release -> genuine power-on: full
 *                                          hardware re-init, the
 *                                          pending long-press release
 *                                          event is swallowed, and
 *                                          the count mode resumes
 *                                          (chirp included) with its
 *                                          value intact - STOP keeps
 *                                          all RAM.                        */

#define SBS_SLEEP  0
#define SBS_AWAIT  1
#define SBS_REARM  2     /* one-slice grace between a detected release
                            and the INT0 re-arm (see below)          */

#define SB_AWAIT_SLICES  50      /* 1s long-press window                */

static unsigned char data sb_phase = SBS_SLEEP;
static unsigned char data sb_timer = 0;

/* empty vector handler: the wake only needs to RETI; everything else
 * happens in Mode_Standby_Slice on the next frame-loop pass          */
void SB_Int0ISR(void) interrupt 0
{
}

/* power the panel down and park every pin safely; from here on only
 * KEY0 (INT0) may end the STOP                                       */
static void Standby_PowerDown(void)
{
    HT1621_BuzzerOff();          /* TONE_OFF                            */
    HT1621_LcdOff();             /* LCD_OFF                             */
    HT1621_SysDisable();         /* SYS_DIS: RC oscillator stops        */

    /* whole-port writes are deliberate here (same exception class as
     * SleepForever): force everything to the quasi-bidirectional idle.
     * P1.1 becomes a high-Z input - the RC cap bleeds into the
     * undriven panel load and VLCD decays to 0V. Keys keep their
     * pull-ups (P3.2 needs one for the INT0 edge), UART idles high. */
    P1M0 = 0x00; P1M1 = 0x00;
    P3M0 = 0x00; P3M1 = 0x00;
    P5M0 = 0x00; P5M1 = 0x00;
    P1 = 0xFF; P3 = 0xFF; P5 = 0xFF;
}

/* arm the deep stop: WKT off, INT0 (falling edge) on. The frame
 * loop's next Slice_SleepOneTick() then sleeps until KEY0.          */
static void Standby_ArmDeepStop(void)
{
    WKTCH &= 0x7F;               /* WKTEN = 0: no slice ticks           */
    IE0 = 0;                     /* drop any edge pending since the
                                    wake - it must not fire the moment
                                    EX0 comes back on                  */
    IT0 = 1;                     /* INT0 falling edge                   */
    EX0 = 1;
    EA  = 1;
}

/* back from the deep stop: restore the no-interrupt invariant and
 * restart the WKT timeslice                                          */
static void Standby_WakeFromDeepStop(void)
{
    EX0 = 0;
    EA  = 0;
    Timeslice_Init();            /* IRCDB + WKTEN: slices run again     */
}

/* full hardware bring-up for the resume; RAM kept all the data      */
static void Standby_ResumeInit(void)
{
    Buttons_Init();              /* key pins: inputs + pull-ups         */
    HT1621_Init();               /* HT1621 bus push-pull idle           */
    VLCD_Reapply();              /* fresh VCC read -> fresh VLCD policy */
    HT1621_SysInit();            /* SYS_EN + RC_256K                    */
    HT1621_BuzzerOff();          /* tone off                            */
    HT1621_LcdOn();              /* BIAS 1/3 4COM + LCD_ON              */
    g_disp_buf[0] = g_disp_buf[1] = g_disp_buf[2] = 0;
    Display_Render();            /* SEG RAM cleared                     */
}

void Mode_Standby_Enter(void)
{
    Standby_PowerDown();
    sb_phase = SBS_SLEEP;
    Standby_ArmDeepStop();       /* the loop's next STOP becomes deep   */
}

void Mode_Standby_Slice(unsigned char events)
{
    if (sb_phase == SBS_SLEEP)
    {
        /* running at all means the INT0 edge ended the deep STOP; the
         * waking press itself is not an event of this mode           */
        Standby_WakeFromDeepStop();
        sb_phase = SBS_AWAIT;
        sb_timer = SB_AWAIT_SLICES;
        return;
    }

    if (sb_phase == SBS_REARM)
    {
        /* one slice of grace after a detected release: 20ms is longer
         * than the whole release bounce, so no falling edges are left
         * in flight when INT0 comes back on. If KEY0 is already down
         * again (a genuine instant re-press, already scanned/armed),
         * run a fresh window instead of sleeping through it.        */
        if (--sb_timer != 0)
            return;
        if ((P3 & 0x04) == 0)    /* KEY0 held: fresh long-press window  */
        {
            sb_phase = SBS_AWAIT;
            sb_timer = SB_AWAIT_SLICES;
        }
        else
        {
            sb_phase = SBS_SLEEP;
            Standby_ArmDeepStop();
        }
        return;
    }

    /* AWAIT: the 1s long-press window. The release check comes first
     * so a release racing the window end counts as an accidental
     * touch (a full second was not held).                            */
    if (events & KEY_EV_K0UP)
    {
        sb_phase = SBS_REARM;    /* accidental touch: one grace slice,
                                    then back to deep sleep            */
        sb_timer = 1;
        return;
    }
    if (--sb_timer != 0)
        return;

    /* full second held: genuine power-on                              */
    Standby_ResumeInit();
    Buttons_Key0Disarm();        /* swallow the pending long-press
                                    release - the count mode must not
                                    see it, by architecture not by
                                    tolerance                          */
    App_SwitchTo(APP_MODE_COUNT);
}
