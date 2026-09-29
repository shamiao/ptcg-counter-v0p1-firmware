#include <stc8h.h>
#include <stdio.h>

#include "hardware-definition.h"
#include "common.h"
#include "buttons.h"
#include "display.h"
#include "ht1621.h"
#include "app.h"
#include "mode_count.h"

/* ------------- mode 0: saturating counter (core business) ------------
 * A plain 0..990 counter in a u16, stepped by the keys: 1=+100,
 * 2/3=+10, 4=-100, 5/6=-10; it clamps at both ends (no wrap). The
 * value is kept in plain units - NOT decades - so a future config-
 * urable step size (100/10/1) drops in without re-representation.
 * Digits are recomputed from the value on every render, so cross-
 * digit carries (90->100 and back) fall out by construction; leading
 * zeros are blanked, zero shows as "  0". The value is LOCAL to this
 * mode: it survives a coin toss untouched (the coin mode never sees
 * it). KEY0 press leaves for the coin mode unconditionally.        */
static unsigned int data count_val = 0;

#define COUNTER_MAX  990u

/* ------------- key-feedback beeper (sliced, never busy-wait) ---------
 * An accepted step plays ONE 40ms chirp; a rejected step (value would
 * not move) plays THREE 20ms ticks with 20ms gaps. The schedule is a
 * tiny ON/GAP state machine ticked once per slice; Mode_Count_Enter
 * clears it, so a switch to coin mode mid-beep can never leave a
 * stale auto-off behind (the app entry chirp owns the buzzer then). */
#define CNT_BEEP_IDLE  0
#define CNT_BEEP_ON    1
#define CNT_BEEP_GAP   2

#define CNT_BEEP_ACCEPT  2      /* 40ms accepted-step chirp           */
#define CNT_BEEP_TICK    1      /* 20ms reject tick                   */
#define CNT_BEEP_TGAP    1      /* 20ms gap between reject ticks      */

static unsigned char data cnt_beep = CNT_BEEP_IDLE;
static unsigned char data cnt_beep_t = 0;
static unsigned char data cnt_beep_n = 0;   /* ONs left incl. current */

static void Count_BeepStart(unsigned char n)
{
    HT1621_Buzzer2kOn();
    cnt_beep = CNT_BEEP_ON;
    cnt_beep_t = (n == 1) ? CNT_BEEP_ACCEPT : CNT_BEEP_TICK;
    cnt_beep_n = n;
}

static void Count_BeepTick(void)
{
    if (cnt_beep == CNT_BEEP_IDLE)
        return;
    if (--cnt_beep_t != 0)
        return;
    if (cnt_beep == CNT_BEEP_ON)
    {
        HT1621_BuzzerOff();
        cnt_beep_n--;
        if (cnt_beep_n != 0)     /* another reject tick follows      */
        {
            cnt_beep = CNT_BEEP_GAP;
            cnt_beep_t = CNT_BEEP_TGAP;
        }
        else
            cnt_beep = CNT_BEEP_IDLE;
    }
    else                         /* GAP expired: fire the next tick  */
    {
        HT1621_Buzzer2kOn();
        cnt_beep = CNT_BEEP_ON;
        cnt_beep_t = CNT_BEEP_TICK;
    }
}

/* returns 1 if the value moved, 0 if the step was rejected          */
static unsigned char Count_ApplyKey(unsigned char key)
{
    switch (key)
    {
    case BUTTON_KEY1:                        /* +100: hundreds digit
                                                only - crossing the
                                                ceiling rejects       */
        if (count_val > COUNTER_MAX - 100u) return 0;
        count_val += 100;
        return 1;
    case BUTTON_KEY2:                        /* +10 (saturating ends) */
    case BUTTON_KEY3:
        if (count_val > COUNTER_MAX - 10u) return 0;
        count_val += 10;
        return 1;
    case BUTTON_KEY4:                        /* -100: hundreds digit
                                                only - below zero
                                                rejects               */
        if (count_val < 100u) return 0;
        count_val -= 100;
        return 1;
    case BUTTON_KEY5:                        /* -10 (saturating ends) */
    case BUTTON_KEY6:
        if (count_val < 10u) return 0;
        count_val -= 10;
        return 1;
    default:                                 /* KEY0 handled in Slice */
        return 0;
    }
}

static void Count_Render(void)
{
    unsigned int data v = count_val;

    g_disp_buf[0] = DISP_EN | (unsigned char)(v % 10u);        /* units  */
    g_disp_buf[1] = (v >= 10u)                                /* tens   */
        ? (unsigned char)(DISP_EN | ((v / 10u) % 10u)) : 0;
    g_disp_buf[2] = (v >= 100u)                               /* hundr. */
        ? (unsigned char)(DISP_EN | (v / 100u)) : 0;
    Display_Render();
}

void Mode_Count_Enter(void)
{
    cnt_beep = CNT_BEEP_IDLE;    /* stale key-beep schedules die here:
                                    the app entry chirp owns the
                                    buzzer from this slice on         */
    cnt_beep_t = 0;
    cnt_beep_n = 0;
    Count_Render();               /* restore the panel this mode owns  */
}

void Mode_Count_Slice(unsigned char events)
{
    unsigned char data press = events & 0x7Fu;   /* mask off KEY0-rel */

    Count_BeepTick();

    if (press == BUTTON_KEY_MAIN)      /* KEY0: unconditional handover */
    {
        App_SwitchTo(APP_MODE_COIN);
        return;
    }

    if (press)
    {
        unsigned char data idx = 0;
        unsigned char data m = press;

        if (Count_ApplyKey(press))
        {
            Count_Render();
            Count_BeepStart(1);    /* accepted: one 40ms chirp        */
        }
        else
        {
            Count_BeepStart(3);    /* rejected: three 20ms ticks      */
        }
        while (m > 1)               /* mask -> key number              */
        {
            m >>= 1;
            idx++;
        }
        {
            unsigned int data v = count_val;
            printf("k%bu v%u\r\n", idx, v);
        }
        DelayMs(1);                 /* stop bit out before STOP        */
    }
    /* the KEY0-release bit is not an event of this mode              */
}
