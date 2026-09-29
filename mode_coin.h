#ifndef __MODE_COIN_H__
#define __MODE_COIN_H__

/* Mode 1: the coin toss ceremony. Enter: shake until KEY0 is
 * released, then blink the result 3x with beeps, hold it 5s, blank
 * 0.5s, and hand the panel back to the count mode. */

void Mode_Coin_Enter(void);
void Mode_Coin_Slice(unsigned char events);

#endif
