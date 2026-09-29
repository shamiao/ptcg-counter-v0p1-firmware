#ifndef __MODE_COUNT_H__
#define __MODE_COUNT_H__

/* Mode 0: the saturating counter. KEY1..6 step it, KEY0 hands the
 * panel over to the coin toss. */

void Mode_Count_Enter(void);
void Mode_Count_Slice(unsigned char events);

#endif
