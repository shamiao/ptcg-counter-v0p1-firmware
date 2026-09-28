#include <stc8h.h>

#include "timeslice.h"

volatile unsigned int data g_cycle = 0;

unsigned int xdata g_fwt_hz;
unsigned int xdata g_wkt_reload;

/* The WKT runs on its own ~32kHz RC oscillator, factory-trimmed but
 * with a wide tolerance (manual 7.11.1); the measured frequency is
 * mirrored in idata F8/F9, big-endian Hz (nominal 32768 = 8000H,
 * manual 10.4.3) - same capture-first rules as the BGV mirror in
 * vlcd.c. */
#define FWT_NOMINAL_HZ  32768u
#define FWT_MIN_HZ      24000u   /* sanity window; outside -> nominal  */
#define FWT_MAX_HZ      40000u

void Timeslice_EarlyInit(void)
{
    unsigned int fwt;
    unsigned long count;
    unsigned char volatile idata *p = (unsigned char volatile idata *)0xF8;

    fwt = ((unsigned int)p[0] << 8) | (unsigned int)p[1];
    g_fwt_hz = fwt;                        /* raw word, for reports    */
    if ((fwt < FWT_MIN_HZ) || (fwt > FWT_MAX_HZ))
        fwt = FWT_NOMINAL_HZ;

    /* manual 7.11.1: T = 1e6 * 16 * count / Fwt (us), register holds
     * count-1; rounded to nearest so the quantization bias stays under
     * half a 0.5ms tick per slice. Sane Fwt keeps count around 40, far
     * from the reserved borders 0 / 32767. */
    count = ((unsigned long)fwt * SLICE_MS + 8000UL) / 16000UL;
    g_wkt_reload = (unsigned int)count - 1u;
}

void Timeslice_Init(void)
{
    P_SW2 |= 0x80;             /* EAXFR: IRCDB is XFR-mapped             */
    IRCDB = 0x10;              /* IRC debounce after STOP wake, manual
                                  7.3.8 recommends 0x10; wrong setting
                                  lets the CPU run off the rails for a
                                  few cycles on each wake                */
    P_SW2 &= ~0x80;

    WKTCL = (unsigned char)g_wkt_reload;
    WKTCH = 0x80 | (unsigned char)(g_wkt_reload >> 8);
                               /* WKTEN=1 + count high bits (zero here)  */
}

void Slice_SleepOneTick(void)
{
    PCON |= 0x02;              /* STOP; WKT overflow is the only wake
                                  event in this firmware                 */
    g_cycle++;                 /* back from STOP == one slice elapsed    */
}
