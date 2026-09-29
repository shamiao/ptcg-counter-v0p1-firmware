#ifndef __APP_H__
#define __APP_H__

/* UI mode dispatcher. Each mode owns its state and gets one call per
 * 20ms slice with the fresh key events; modes never block and never
 * touch the WKT - the timeslice wakeup is a system-wide hard rule. */

#define APP_MODE_COUNT    0    /* the plain 0..990 counter (default)  */
#define APP_MODE_COIN     1    /* the coin toss ceremony              */
#define APP_MODE_STANDBY  2    /* deep sleep, KEY0 long-press wakes    */

/* One event word per slice: bits 0..6 carry the accepted single-key
 * press mask (see buttons.h), bit 7 carries the KEY0 release event.
 * Both may arrive in the same slice.                                */
#define KEY_EV_K0UP     0x80u

void App_Slice(unsigned char events);
void App_SwitchTo(unsigned char mode);   /* runs the new mode's enter
                                            hook (renders its panel,
                                            chirps the entry beep)    */
void App_BeepCancel(void);               /* drop a running entry chirp
                                            (caller owns the buzzer)  */

#endif
