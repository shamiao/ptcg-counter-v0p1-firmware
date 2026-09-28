#include <stc8h.h>

#include "hardware-definition.h"
#include "common.h"
#include "vlcd.h"

/* ---- factory BGV location (manual 7.3.12) ----
 * The factory calibration data mirrors into idata at reset: BGV (mV,
 * BIG-ENDIAN: 0xEF = high byte, 0xF0 = low byte) at 0xEF/0xF0, the 32K
 * IRC trim at 0xF1-0xF7, the 24MHz IRC trim at 0xF8/0xF9 - the official
 * ADC "reverse VCC" demo reads exactly there: BGV = (int idata *)0xef;
 * Keil STARTUP.A51 (IDATALEN=0x80) clears only 0x00-0x7F, so the mirror
 * survives into main(); VLCD_EarlyInit() copies the BGV bytes out as
 * the very first act of main(), before stack growth could ever reach
 * high idata.                                                          */
#define BGV_CAL_MV  1190

/* the two mirror bytes (0xEF, 0xF0), captured by VLCD_EarlyInit() */
static unsigned char xdata g_bgv_mirror[2];

/* debug-report globals live in xdata to spare the 128-byte DATA space;
 * each is written before its first read (XRAM is not cleared at boot) */
unsigned int xdata g_adc15_n;   /* last ADC15 average, kept for report */
unsigned int xdata g_bgv_raw;   /* raw factory BGV word, for diagnosis */

/* main() MUST call this first: capture the factory BGV from the idata
 * mirror before anything (stack growth, C runtime) can touch it.     */
void VLCD_EarlyInit(void)
{
    unsigned char volatile idata *p =
        (unsigned char volatile idata *)0xEF;
    g_bgv_mirror[0] = p[0];     /* 0xEF: BGV high byte */
    g_bgv_mirror[1] = p[1];     /* 0xF0: BGV low byte  */
}

unsigned int ReadBGVmV(void)
{
    unsigned int bgv;

    /* idata 0xEF/0xF0 factory mirror, big-endian mV */
    bgv = ((unsigned int)g_bgv_mirror[0] << 8)
        | (unsigned int)g_bgv_mirror[1];
    g_bgv_raw = bgv;                        /* keep raw for debug print  */
    if ((bgv < 1000) || (bgv > 1400))
        bgv = BGV_CAL_MV;                   /* nominal fallback          */
    return bgv;
}

/* ---- VCC measurement via ADC (datasheet ch.10.5 "反推VCC" method) ----
 * ADC15 is internally fixed to the ~1.19V bandgap (BGV).
 * VREF+ = VCC, 10-bit ADC on STC8H1K08:
 *     N = BGV/VCC * 1024   =>   VCC_mV = BGV_mV * 1024 / N           */
static unsigned int ADC15_Avg64(void)           /* 64-sample average */
{
    unsigned char i;
    unsigned long sum = 0;

    P_SW2  |= 0x80;             /* ADCTIM is XFR-mapped                  */
    ADCTIM   = 0x3F;            /* sample window (official STC-ISP tpl)  */
    ADCCFG   = 0x2F;            /* RESFMT=1: 10-bit right-justified;
                                   SPEED=15: ADC clk = SYSclk/32        */
    ADC_CONTR = 0x8F;           /* ADC_POWER=1, CHS=15: internal 1.19V   */
    DelayMs(1);                 /* ADC power-up settling                */
    for (i = 0; i < 64; i++)
    {
        ADC_CONTR &= ~0x20;     /* clear ADC_FLAG                       */
        ADC_CONTR |= 0x40;      /* ADC_START                            */
        while (!(ADC_CONTR & 0x20));            /* wait conversion done */
        sum += ((unsigned int)(ADC_RES & 0x03) << 8) | ADC_RESL;
    }
    ADC_CONTR = 0x00;           /* ADC off: save battery afterwards     */
    ADCCFG    = 0x00;
    g_adc15_n = (unsigned int)(sum >> 6);   /* remember for report   */
    return g_adc15_n;
}

/* returns VCC in mV, 0 if the reading is invalid; an absurdly high
 * computed value (degenerate ADC code) clamps to 65535 instead of
 * wrapping, so the overvoltage gate still catches it */
unsigned int VCC_MeasureMv(void)
{
    unsigned long v;
    unsigned int n = ADC15_Avg64();
    if ((n == 0) || (n >= 1023))            /* saturated or broken */
        return 0;
    v = (unsigned long)ReadBGVmV() * 1024UL / n;
    if (v > 65535UL)
        v = 65535UL;
    return (unsigned int)v;
}

/* ---- VLCD PWM (P1.1 = PWM1N of PWMA channel 1) ----
 * period = VLCD_PWM_PERIOD counts -> f = 6MHz/120 = 50kHz exactly
 * MEASURED ON SILICON: PWM1N pin duty-high = CCR1/(ARR+1),
 * i.e. PWM1N here outputs OC1REF NON-inverted (not complementary).
 *
 * Drive network: R = 1.8kohm + C = 1uF (tau = 1.8ms >> 20us PWM
 * period), so VLCD is a clean DC rail (ripple ~10mV), NOT the
 * chopped square wave of the old 1-ohm direct-drive experiments.
 * Bench: HT1621 bias ladder + lit panel draws I_load ~= 35uA
 * (measured: R=10k capped VLCD at 2952mV @VCC=3300mV), drooping the
 * rail I_load*R ~= 63mV below the ideal divider value.
 *
 * DC model:   VLCD = VCC * CCR/120 - 63mV
 * Control law (coarse on purpose - the panel tolerates it), applied
 * ONCE at boot; continuous regulation is a later, separate design:
 *   VCC <= 3.263V : no PWM - P1.1 push-pull high (best effort:
 *                   VLCD = VCC - 63mV, target unreachable)
 *   VCC  > 3.263V : CCR = 120*(3200+63)mV/VCC, rounded, hard-floored
 *                  at 60% duty (3.3V->99%, 4.5V->72%, 5.4V->60%)
 *   ADC failed    : the 60% duty floor
 *   VCC >= 5.5V   : gated by the boot overvoltage check in main()
 *                   (VCC_OVERVOLT_MV): print the fault, then jump to
 *                   the shared fatal terminator SleepForever().
 * (History: the earlier "reading = VCC*sqrt(duty)" law described the
 *  1-ohm direct-chopped drive; it is void with this RC filter.)    */
#define PWM_PERIOD        VLCD_PWM_PERIOD   /* ARR+1, PWM clock counts  */
#define VLCD_REG_MV       3200UL  /* regulate DC VLCD to ~3.2V          */
#define VLCD_IR_DROP_MV   63UL    /* 35uA load x 1.8kohm series R       */
#define VCC_FULL_DUTY_MV  (VLCD_REG_MV + VLCD_IR_DROP_MV) /* 100% zone  */
#define PWM_CCR_FLOOR     ((PWM_PERIOD * 3UL) / 5UL)  /* 60% floor = 72 */

/* PWM1N on P1.1, 50kHz; ccr = high counts (duty = ccr/PWM_PERIOD) */
static void VLCD_PWM_Init(unsigned int ccr)
{
    P1M0 |= 0x02;                   /* P1.1 push-pull (masked)         */
    P1M1 &= ~0x02;
    P_SW2 |= 0x80;                  /* EAXFR=1: PWMA regs are XFR-mapped */

    PWMA_CCER1 = 0x00;              /* disable ch1 outputs while config  */
    PWMA_CCMR1 = 0x60;              /* OC1M=110: PWM mode 1               */
    PWMA_PSCR  = 0;                 /* PWM clock = SYSclk (no divide)     */
    PWMA_ARR   = PWM_PERIOD - 1;    /* 119: period 120 counts             */
    PWMA_CCR1  = ccr;               /* duty set from measured VCC         */
    PWMA_RCR   = 0;                 /* no repetition                      */
    PWMA_DTR   = 0;                 /* no dead time                       */
    PWMA_PS    = 0x00;              /* PWM1 group -> P1.0/P1.1            */
    PWMA_BKR   = 0x80;              /* MOE=1: main output enable          */
    PWMA_CCER1 = 0x05;              /* CC1E + CC1NE, active high          */
    PWMA_ENO   = 0x02;              /* ENO1N only: P1.1 on, P1.0 stays KEY6 */
    PWMA_CR1  |= 0x01;              /* CEN=1: start, edge-aligned up      */
}

/* PWM off: hand P1.1 back to the GPIO latch (push-pull), driving it
 * high (= 100% duty, VLCD = VCC) or low (VLCD = 0V, panel undriven) */
void VLCD_PWM_Off(unsigned char drive_high)
{
    P1M0 |= 0x02;                   /* P1.1 push-pull (masked)         */
    P1M1 &= ~0x02;
    P_SW2 |= 0x80;                  /* EAXFR=1: PWMA regs are XFR-mapped */
    PWMA_CCER1 = 0x00;              /* disable ch1 outputs               */
    PWMA_ENO  &= ~0x02;             /* ENO1N=0: P1.1 back to GPIO        */
    PWMA_CR1  &= ~0x01;             /* CEN=0: stop the PWM timebase      */
    P11 = drive_high ? 1 : 0;
}

/* Set the VLCD drive for this VCC reading (mV); returns the CCR in
 * use (0 = PWM off, P1.1 push-pull high). The duty never drops below
 * PWM_CCR_FLOOR (60%), whatever VCC claims; vcc_mv == 0 (ADC failed)
 * also gets the floor.                                              */
unsigned int VLCD_SetByVccMv(unsigned int vcc_mv)
{
    unsigned int ccr;

    if (vcc_mv == 0)                       /* ADC failed: 60% floor   */
    {
        VLCD_PWM_Init(PWM_CCR_FLOOR);
        return PWM_CCR_FLOOR;
    }
    if (vcc_mv <= VCC_FULL_DUTY_MV)        /* target unreachable: max out */
    {
        VLCD_PWM_Off(1);                   /* P1.1 push-pull high = 100% */
        return 0;
    }
    ccr = (unsigned int)(((unsigned long)PWM_PERIOD
           * (VLCD_REG_MV + VLCD_IR_DROP_MV) + vcc_mv / 2) / vcc_mv);
    if (ccr < PWM_CCR_FLOOR)               /* hard 60% duty floor */
        ccr = PWM_CCR_FLOOR;
    VLCD_PWM_Init(ccr);
    return ccr;
}

/* Re-set the VLCD drive from a fresh VCC measurement: just the two
 * steps, measure then set - no overvoltage gating inside (the boot
 * gate is one-shot by design). Returns the CCR now in use.          */
unsigned int VLCD_Reapply(void)
{
    return VLCD_SetByVccMv(VCC_MeasureMv());
}

/* Software "oscilloscope": sample P1.1 ~30000 times, return low-permille
 * (0..1000). Used at boot to self-check the pin duty (PWM mode) or the
 * constant-high drive (GPIO mode). LIMIT: one sampler pass is ~1.4us, so
 * a low pulse of only a few PWM counts (CCR=118 -> 2 SYSclk = 333ns) is
 * never caught: near-100% duties legitimately read 0pm (CCR=118 ideal
 * is 17pm); the check only resolves noticeably-wide pulses (say
 * CCR <= 110).                                                          */
unsigned int MeasureP11LowPermillage(void)
{
    unsigned long low = 0, n;
    for (n = 0; n < 30000UL; n++)
        if ((P1 & 0x02) == 0) low++;
    return (unsigned int)(low / 30UL);   /* 30000/1000 -> permille */
}
