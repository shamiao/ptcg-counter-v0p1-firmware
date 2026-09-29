#include "ht1621.h"
#include "buttons.h"
#include "display.h"
#include "app.h"
#include "mode_coin.h"

/* ------------- mode 1: coin toss (all timings in 20ms slices) --------
 * Everything here is a phase machine driven by one call per slice;
 * no busy-wait anywhere - the timeslice wakeup is a system rule.
 *
 * Timeline after KEY0 is released (result = the face on the panel at
 * that instant):
 *   shake "U P"/"DON" alternated every 80ms   (while KEY0 is held)
 *   blink the result - pattern and pace depend on the face:
 *     UP : on 300ms (2kHz beep) / off 300ms, x2, final on 300ms  (dense)
 *     DON: on 500ms (2kHz beep) / off 500ms, x1, final on 500ms  (calm)
 *     both blocks last exactly 1.5s and end shown
 *   hold:  the face solid for 5s, then blank 0.3s
 *   then App_SwitchTo(APP_MODE_COUNT) restores the counter panel.
 *
 * Shake rate: 80ms per face - deliberately faster than the TN panel's
 * ~100ms full response, so the faces ghost slightly into each other;
 * that blur is accepted (bench-tuned): the alternation still reads as
 * a vigorous shake. Tunable via COIN_SHAKE_SLICES.
 *
 * Key handling inside this mode: while shaking, ONLY the KEY0 release
 * matters (it stops the shake and locks the face). During the result
 * animation, a fresh KEY0 press re-rolls the coin ONLY in the solid-
 * display phase (the 5s hold): the animation is aborted, one short
 * beep plays, and the shake restarts. In the blink phases and the
 * final blank a KEY0 press is a deliberate no-op (release and press
 * again during the hold). The re-roll press must be a clean accepted
 * one: a press merged into a multi-press produces no event at all.
 * Every other key event is ignored in this mode.                    */

#define COIN_SHAKE_SLICES   4     /* 80ms per face                      */
#define COIN_UP_BLINK       15    /* 300ms blink phase - UP is dense    */
#define COIN_DON_BLINK      25    /* 500ms blink phase - DON is calm    */
/* UP blinks 5 phases, DON 3: both blocks last exactly 1.5s           */
#define COIN_HOLD_ON        250   /* 5s (u8 timer ceiling is 255)       */
#define COIN_HOLD_OFF       15    /* 0.3s                               */

#define UP_BLINK_ONS        3     /* UP:   on-off-on-off-on (5 phases)  */
#define DON_BLINK_ONS       2     /* DON:  on-off-on      (3 phases)    */
/* blink_left counts ON phases only: it decrements at each ON expiry
 * and the final ON flows straight into the hold (no trailing off).   */

#define CP_SHAKE     0
#define CP_BLINK_ON  1
#define CP_BLINK_OFF 2
#define CP_HOLD_ON   3
#define CP_HOLD_OFF  4

static unsigned char data coin_face = 0;      /* 0 = "UP", 1 = "DON"    */
static unsigned char data coin_phase = CP_SHAKE;
static unsigned char data blink_left = 0;
static unsigned char data phase_timer = 0;

static void Coin_ShowFace(void)
{
    if (coin_face == 0)
    {
        g_disp_buf[2] = DISP_EN | DISP_GLYPH('U');
        g_disp_buf[1] = 0;                    /* "U P": spaced digits  */
        g_disp_buf[0] = DISP_EN | DISP_GLYPH('P');
    }
    else
    {
        g_disp_buf[2] = DISP_EN | DISP_GLYPH('D');
        g_disp_buf[1] = DISP_EN | DISP_GLYPH('O');
        g_disp_buf[0] = DISP_EN | DISP_GLYPH('N');
    }
    Display_Render();
}

static void Coin_Blank(void)
{
    g_disp_buf[0] = g_disp_buf[1] = g_disp_buf[2] = 0;
    Display_Render();
}

/* blink phase length depends on the face: UP dense, DON calm, both
 * blocks exactly 1.5s in total                                              */
static unsigned char Coin_BlinkLen(void)
{
    return (coin_face == 0) ? COIN_UP_BLINK : COIN_DON_BLINK;
}

/* (re)start the shake: deterministic face, fresh period timer - the
 * entry hook and the re-roll path share this                         */
static void Coin_StartShake(void)
{
    coin_face = 0;               /* deterministic start; the result
                                    comes from the release timing     */
    coin_phase = CP_SHAKE;
    phase_timer = COIN_SHAKE_SLICES;
    Coin_ShowFace();
}

void Mode_Coin_Enter(void)
{
    /* THE exception to the KEY0-release boundary rule: the entering
     * press was accepted in the count mode (previous generation), and
     * its release IS this mode's stop-shake event - adopt it.        */
    Buttons_Key0Adopt();
    Coin_StartShake();
}

void Mode_Coin_Slice(unsigned char events)
{
    if (coin_phase == CP_SHAKE)
    {
        if (events & KEY_EV_K0UP)   /* KEY0 released: lock the face    */
        {
            coin_phase = CP_BLINK_ON;
            blink_left = (coin_face == 0) ? UP_BLINK_ONS
                                          : DON_BLINK_ONS;
            phase_timer = Coin_BlinkLen();
            Coin_ShowFace();       /* the face shown now is the result */
            App_BeepCancel();      /* a fast tap may still have the
                                      entry chirp running - its auto-
                                      off must not cut this beep     */
            HT1621_Buzzer2kOn();
            return;
        }
        if (--phase_timer == 0)
        {
            phase_timer = COIN_SHAKE_SLICES;
            coin_face ^= 1;
            Coin_ShowFace();
        }
        return;                    /* key presses mean nothing here    */
    }

    /* result animation: a fresh KEY0 press re-rolls the coin, but only
     * in the solid-display phase - during the blinks and the final
     * blank it is a deliberate no-op. Checked before the phase timer
     * so a press racing the last slice wins (event-first, same
     * ordering rule as the standby window). The buzzer is off
     * throughout HOLD_ON, so the chirp always starts clean.         */
    if (coin_phase == CP_HOLD_ON && (events & 0x7Fu) == BUTTON_KEY_MAIN)
    {
        App_BeepOnce();
        Coin_StartShake();
        return;
    }

    /* purely time-driven from here on                                */
    if (--phase_timer != 0)
        return;

    switch (coin_phase)
    {
    case CP_BLINK_ON:
        HT1621_BuzzerOff();
        if (--blink_left == 0)     /* the final blink ON flows straight
                                      into the hold (no gap, no extra
                                      blank phase)                    */
        {
            coin_phase = CP_HOLD_ON;
            phase_timer = COIN_HOLD_ON;   /* face stays shown        */
        }
        else
        {
            Coin_Blank();
            coin_phase = CP_BLINK_OFF;
            phase_timer = Coin_BlinkLen();
        }
        break;
    case CP_BLINK_OFF:
        Coin_ShowFace();
        coin_phase = CP_BLINK_ON;
        phase_timer = Coin_BlinkLen();
        HT1621_Buzzer2kOn();
        break;
    case CP_HOLD_ON:
        Coin_Blank();
        coin_phase = CP_HOLD_OFF;
        phase_timer = COIN_HOLD_OFF;
        break;
    case CP_HOLD_OFF:
        App_SwitchTo(APP_MODE_COUNT);      /* ceremony over           */
        break;
    }
}
