#ifndef __COMMON_H__
#define __COMMON_H__

/* Shared helpers used across modules. MAIN_FOSC_HZ (hardware-definition.h)
 * sets the delay scaling. */

void DelayMs(unsigned int ms);   /* rough, ~1ms per unit              */
void DelayUs(unsigned int us);   /* >= 1us per unit (1T core)         */
char putchar(char c);            /* Keil C51 printf() retarget        */
void SleepForever(void);         /* fatal halt: safe IO + power-down  */

#endif
