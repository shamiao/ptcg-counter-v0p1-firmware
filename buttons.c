#include <stc8h.h>

#include "hardware-definition.h"
#include "buttons.h"

/* 7 keys, active low (external pull-up + internal pull-up enabled):
 * KEY_MAIN=P3.2, KEY1..KEY5=P3.3..P3.7, KEY6=P1.0. */

#define BUTTONS_P3_MASK  0xFC    /* P3.2..P3.7, excludes UART P3.0/P3.1 */
#define BUTTONS_P1_MASK  0x01    /* P1.0 = KEY6                          */

void Buttons_Init(void)
{
    P_SW2 |= 0x80;                /* PxPU registers are XFR-mapped       */

    P3M0 &= ~BUTTONS_P3_MASK;     /* key pins high-Z input (masked)      */
    P3M1 |= BUTTONS_P3_MASK;
    P3PU |= BUTTONS_P3_MASK;      /* internal weak pull-ups              */
    P3   |= BUTTONS_P3_MASK;      /* keep write latches high             */

    P1M0 &= ~BUTTONS_P1_MASK;     /* P1.0 same treatment (masked, keeps
                                     P1.1 VLCD / P1.2,5,6,7 HT1621)   */
    P1M1 |= BUTTONS_P1_MASK;
    P1PU |= BUTTONS_P1_MASK;
    P1   |= BUTTONS_P1_MASK;
}

/* returns a bitmask of currently PRESSED keys (1 = pressed):
 * bit0=KEY_MAIN, bit1..5=KEY1..KEY5, bit6=KEY6, bit7 spare */
unsigned char Buttons_Read(void)
{
    unsigned char s;

    s = (unsigned char)((~P3 & BUTTONS_P3_MASK) >> 2);  /* P3.2..7 -> bits 0..5 */
    if ((P1 & BUTTONS_P1_MASK) == 0)
        s |= BUTTON_KEY6;
    return s;
}

/* ------------- key state machine (one step per 20ms slice) ------------
 * S0 --(exactly 1 key)--> S1   event fires ONCE, on the edge
 * any --(2+ keys)--------> S2  ghost/rollover block; no event ever
 * S1/S2 --(0 keys)-------> S0  only a full release re-arms
 *
 * A sample is classified atomically into 0 / exactly-1 / 2+ keys, so a
 * two-key press can never leak a single-key event: if both keys are
 * down by the first sample that sees either, that sample reads 2+.
 * In S1 a different key cannot appear without passing through 2+ (S2)
 * or 0 (S0), so no identity tracking is needed.
 * Debouncing is the 20ms cadence: bounce shorter than one slice is
 * invisible. Double-fire would need a key bouncing open > 20ms while
 * held - bad-switch territory (hardening option: 2 equal samples).  */
#define KFSM_S0  0
#define KFSM_S1  1
#define KFSM_S2  2

static unsigned char data key_fsm = KFSM_S0;
static unsigned char data k0_armed = 0;   /* KEY0 release tracker   */

unsigned char Buttons_Scan(void)
{
    unsigned char mask = Buttons_Read();

    if (mask == 0)
    {
        key_fsm = KFSM_S0;          /* full release re-arms the edge    */
        return 0;
    }
    if (mask & (mask - 1))          /* two or more bits set             */
    {
        key_fsm = KFSM_S2;          /* blocked until everything is out  */
        return 0;
    }

    if (key_fsm == KFSM_S0)         /* exactly one key, fresh edge      */
    {
        key_fsm = KFSM_S1;
        if (mask == BUTTON_KEY_MAIN)
            k0_armed = 1;           /* arm the release tracker          */
        return mask;                /* accepted: fire the key logic     */
    }
    return 0;   /* S1 same key held (no repeat); S2 drained to one key
                   stays blocked until zero                            */
}

/* ------------- KEY0 (KEY_MAIN) release tracker ------------------------
 * The second, orthogonal machine. The main FSM above classifies the
 * whole keyboard by COUNT (0 / 1 / 2+) and must stay purely
 * count-driven; "did the accepted KEY0 press just end?" is a separate
 * one-bit question, tracked here instead of polluting the main FSM
 * with a KEY0-level arc out of S2.
 *
 * k0_armed is set on the accepted KEY0 press edge (the only coupling
 * line between the two machines). While armed, each slice tests the
 * KEY0 pin itself: released -> fire ONCE and disarm. This covers
 *   - S1 -> full release (normal case),
 *   - S2 -> KEY0 released while other keys still held: the event
 *     fires, the main FSM stays in S2 until everything is out, and a
 *     later re-press of KEY0 inside S2 cannot re-arm it.
 * A multi-press whose ORIGINAL key was not KEY0 never arms this.   */
unsigned char Buttons_Key0Released(void)
{
    if (k0_armed && (P3 & 0x04))    /* P3.2 = KEY0, active low          */
    {
        k0_armed = 0;
        return 1;
    }
    return 0;
}
