#ifndef __HT1621_H__
#define __HT1621_H__

/* HT1621B segment-LCD driver, bit-banged on P1.2/P1.5/P1.6/P1.7. */

void HT1621_Init(void);          /* port modes + bus idle levels       */
void HT1621_SysInit(void);       /* SYS_EN + RC_256K (before TONE/LCD) */
void HT1621_BeepTest(void);      /* buzzer + oscillator check          */
void HT1621_AllSegmentsOn(void); /* BIAS + LCD_ON + RMW all-on         */
void HT1621_DataFloatTest(void); /* diag: DATA line pull-up present?   */
void HT1621_ReadDump(void);      /* diag: pure READ of all 32 SEG      */

#endif
