#ifndef __TIMESLICE_H__
#define __TIMESLICE_H__

/* 20ms slice loop (console-style frame loop).
 *
 * The STC8H WKT is wake-only: no interrupt vector, no flag, no IE bit -
 * its only observable event is pulling the core out of STOP (PCON.1).
 * One loop iteration = one slice: do the work, sleep, wake at the next
 * WKT overflow, g_cycle++. Work overrunning one WKT period just makes
 * the slice take the next period (dropped frame / lag) - accepted by
 * design, business work per slice is far below 20ms. */

#define SLICE_MS  20

/* data-space volatile; main-flow only (the sole ISR in this firmware
 * is standby's empty INT0 wake handler, which touches nothing).
 * Wraps naturally at 65536 slices = ~21.8 min; business code needing
 * longer durations keeps its own counters. */
extern volatile unsigned int data g_cycle;   /* slices run so far */

/* WKT calibration, computed once at boot (xdata, written before read) */
extern unsigned int xdata g_fwt_hz;     /* factory WKT clock, Hz     */
extern unsigned int xdata g_wkt_reload; /* {WKTCH[6:0],WKTCL} value */

void Timeslice_EarlyInit(void);  /* snapshot idata F8/F9, compute the
                                    reload; one of the very first
                                    statements of main()               */
void Timeslice_Init(void);      /* IRCDB + WKT: 20ms periodic STOP wake */
void Slice_SleepOneTick(void);  /* STOP until WKT wake, then g_cycle++ */

#endif
