#ifndef __VLCD_H__
#define __VLCD_H__

/* VLCD bias rail (P1.1 = PWMA CH1 PWM1N, RC-filtered DC), VCC measurement
 * via the internal bandgap, and the factory BGV capture from idata. */

#define VLCD_PWM_PERIOD 120      /* ARR+1 PWM counts; duty = ccr/period */

/* debug-report values (xdata, written before first read) */
extern unsigned int xdata g_adc15_n;   /* last ADC15 average           */
extern unsigned int xdata g_bgv_raw;   /* raw factory BGV word (mV)    */

void VLCD_EarlyInit(void);       /* MUST be the FIRST call of main()  */
unsigned int ReadBGVmV(void);    /* BGV in mV: mirror, nominal fallback */
unsigned int VCC_MeasureMv(void);   /* VCC in mV; 0 = invalid, high clamp */
unsigned int VLCD_SetByVccMv(unsigned int vcc_mv);
    /* set VLCD for this VCC (mV): returns the CCR in use,
     * 0 = PWM off, P1.1 push-pull high. vcc_mv == 0 -> 60% floor.   */
unsigned int VLCD_Reapply(void);
    /* re-set VLCD: fresh VCC measurement, then set (no gate inside) */
void VLCD_PWM_Off(unsigned char drive_high);
unsigned int MeasureP11LowPermillage(void);  /* self-test: pin duty    */

#endif
