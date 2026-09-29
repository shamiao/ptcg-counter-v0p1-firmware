#include <stc8h.h>
#include <stdio.h>

#include "ht1621.h"
#include "common.h"
#include "buttons.h"
#include "app.h"
#include "mode_count.h"
#include "mode_coin.h"
#include "mode_standby.h"

/* The one global: which mode owns the panel right now. Everything
 * else (counter value, coin phase) is static inside its mode file. */
static unsigned char data app_mode = APP_MODE_STANDBY;  /* zero-init;
                                   the boot traversal sets the real
                                   mode before the first App_Slice   */

/* Mode-entry chirp: a short 2kHz beep on every App_SwitchTo (boot
 * entry included). Switched on at entry, switched off by the slice
 * countdown below - never by a busy-wait. If a mode takes the buzzer
 * over (the coin blink), it cancels the timer via App_BeepCancel.
 * APP_MODE_STANDBY is exempt: it is a house-keeping state (deep
 * sleep), not a business mode.                                       */
#define APP_BEEP_SLICES  5      /* 100ms                             */

static unsigned char data beep_timer = 0;

/* Idle watch: 30 min without any key event hands control to the
 * standby mode (deep STOP, KEY0 long-press to resume) - one PTCG
 * game length. Any accepted press or KEY0-release event counts as
 * an operation. Two-stage counter: 30 min = 90000 slices overflows
 * a u16 (65536 slices = ~21.8 min), so slices accumulate into an
 * u8 minute counter.                                                    */
#define APP_IDLE_MIN_SLICES  3000u  /* one minute of slices          */
#define APP_IDLE_MINUTES     30     /* minutes -> standby            */

static unsigned int data idle_slices = 0;
static unsigned char data idle_minutes = 0;

void App_BeepOnce(void)
{
    HT1621_Buzzer2kOn();         /* on now; the slice countdown in
                                    App_Slice switches it off - the
                                    primitive never busy-waits       */
    beep_timer = APP_BEEP_SLICES;
}

void App_BeepCancel(void)
{
    beep_timer = 0;             /* buzzer state stays as the caller
                                   left it - only the auto-off dies  */
}

void App_SwitchTo(unsigned char mode)
{
    app_mode = mode;
    Buttons_NotifyModeSwitch();   /* KEY0-release boundary rule: every
                                     mode change moves the generation */
    if (mode != APP_MODE_STANDBY)   /* no chirp into deep sleep       */
        App_BeepOnce();             /* entry chirp                    */
    switch (mode)
    {
    case APP_MODE_COUNT:
        Mode_Count_Enter();
        break;
    case APP_MODE_COIN:
        Mode_Coin_Enter();
        break;
    case APP_MODE_STANDBY:
        Mode_Standby_Enter();
        break;
    }
}

void App_BootViaStandby(void)
{
    app_mode = APP_MODE_STANDBY;    /* no chirp: house-keeping state  */
    Buttons_NotifyModeSwitch();     /* keep the every-change invariant */
    Mode_Standby_Boot();            /* straight into the wake end:
                                       power gates, resume, then on
                                       to the count mode             */
}

void App_Slice(unsigned char events)
{
    if (beep_timer != 0 && --beep_timer == 0)
        HT1621_BuzzerOff();     /* chirp done                        */

    if (app_mode != APP_MODE_STANDBY)
    {
        if (events)
        {
            idle_slices = 0;    /* any key event is an operation     */
            idle_minutes = 0;
        }
        else if (++idle_slices >= APP_IDLE_MIN_SLICES)
        {
            idle_slices = 0;
            if (++idle_minutes >= APP_IDLE_MINUTES)
            {
                unsigned int data m_rep = idle_minutes;
                idle_minutes = 0;
                printf("idle=%umin STANDBY\r\n", m_rep);
                DelayMs(1);     /* stop bit out before STOP          */
                App_SwitchTo(APP_MODE_STANDBY);  /* arms the deep
                                                     stop             */
                return;         /* standby owns the machine now     */
            }
            {   /* idle trace, one line per idle minute; doubles as
                   the liveness trace now that ts is gone           */
                unsigned int data m_rep = idle_minutes;
                printf("idle=%umin\r\n", m_rep);
                DelayMs(1);     /* stop bit out before STOP          */
            }
        }
    }

    switch (app_mode)
    {
    case APP_MODE_COUNT:
        Mode_Count_Slice(events);
        break;
    case APP_MODE_COIN:
        Mode_Coin_Slice(events);
        break;
    case APP_MODE_STANDBY:
        Mode_Standby_Slice(events);
        break;
    }
}
