#ifndef __MODE_STANDBY_H__
#define __MODE_STANDBY_H__

/* Mode 2: standby ("pre-power-on"). Entered after 1 min without any
 * key event: panel and tone off, pins parked, WKT stopped, deep STOP
 * until an INT0 (KEY0) falling edge. The wake opens a 1s long-press
 * window on the normal timeslice: releasing KEY0 inside it is an
 * accidental touch (straight back to deep STOP); holding the full
 * second re-initializes the hardware and resumes the count mode with
 * all data intact. House-keeping state, not a business mode: no
 * entry chirp. */

void Mode_Standby_Enter(void);
void Mode_Standby_Slice(unsigned char events);

#endif
