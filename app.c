#include <stc8h.h>

#include "ht1621.h"
#include "app.h"
#include "mode_count.h"
#include "mode_coin.h"
#include "mode_standby.h"

/* The one global: which mode owns the panel right now. Everything
 * else (counter value, coin phase) is static inside its mode file. */
static unsigned char data app_mode = APP_MODE_COUNT;

/* Mode-entry chirp: a short 2kHz beep on every App_SwitchTo (boot
 * entry included). Switched on at entry, switched off by the slice
 * countdown below - never by a busy-wait. If a mode takes the buzzer
 * over (the coin blink), it cancels the timer via App_BeepCancel.
 * APP_MODE_STANDBY is exempt: it is a house-keeping state (deep
 * sleep), not a business mode.                                       */
#define APP_BEEP_SLICES  5      /* 100ms                             */

static unsigned char data beep_timer = 0;

/* Idle watch: 1 min without any key event hands control to the
 * standby mode (deep STOP, KEY0 long-press to resume). Any accepted
 * press or KEY0-release event counts as an operation.              */
#define APP_IDLE_SLICES  3000u  /* 60s x 50 slices                   */

static unsigned int data idle_slices = 0;

void App_BeepCancel(void)
{
    beep_timer = 0;             /* buzzer state stays as the caller
                                   left it - only the auto-off dies  */
}

void App_SwitchTo(unsigned char mode)
{
    app_mode = mode;
    if (mode != APP_MODE_STANDBY)   /* no chirp into deep sleep       */
    {
        HT1621_Buzzer2kOn();    /* entry chirp on; sliced off later  */
        beep_timer = APP_BEEP_SLICES;
    }
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

void App_Slice(unsigned char events)
{
    if (beep_timer != 0 && --beep_timer == 0)
        HT1621_BuzzerOff();     /* chirp done                        */

    if (app_mode != APP_MODE_STANDBY)
    {
        if (events)
            idle_slices = 0;    /* any key event is an operation     */
        else if (++idle_slices >= APP_IDLE_SLICES)
        {
            idle_slices = 0;
            App_SwitchTo(APP_MODE_STANDBY);   /* arms the deep stop */
            return;             /* standby owns the machine now     */
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
