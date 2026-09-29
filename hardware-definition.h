#ifndef __HARDWARE_DEFINITION_H__
#define __HARDWARE_DEFINITION_H__

/* ---- global build parameters (referenced by every module) ---- */
#define MAIN_FOSC_HZ     6000000UL  /* IRC frequency; MUST match the
                                       frequency set in STC-ISP/stcgal */
#define UART_BAUDRATE    9600UL     /* UART1 8N1, Timer1 1T auto-reload */
#define VCC_OVERVOLT_MV  5500UL     /* boot overvoltage gate threshold  */
#define VCC_HALT_MV       2750UL    /* standby wake: refuse to boot
                                       below this (dead battery)       */
#define VCC_LOWBAT_MV     2850UL    /* standby wake: low-battery warn   */

/** 
Hardware definition of this project
(this file contains only text comments description)
*/

/**
MCU STC8H1K08 (TSSOP-20) IO definition
====================
P1.0 KEY6 external pull-up key, internal pull-up enabled
P1.1 HT1621_VLCD push-pull, STC internal PWM (RC filtered)
P1.2 HT1621_RD push-pull
P1.3 NC
P1.4 NC
P1.5 HT1621_CS push-pull
P1.6 HT1621_WR push-pull
P1.7 HT1621_DATA push-pull
P3.0 RXD  (quasi-bidirectional)
P3.1 TXD  (quasi-bidirectional)
P3.2 KEY_MAIN external pull-up key, internal pull-up, INT0
P3.3 KEY1  external pull-up key, internal pull-up
P3.4 KEY2  external pull-up key, internal pull-up
P3.5 KEY3  external pull-up key, internal pull-up
P3.6 KEY4  external pull-up key, internal pull-up
P3.7 KEY5  external pull-up key, internal pull-up
P5.4 NC
*/

/**
Factory calibration mirror in high idata (0xEF..0xF9)
====================
Listed bytes are the ones with a manual source; 0xF1..0xF7 are
undocumented. All big-endian. Capture-first discipline: STARTUP.A51
(IDATALEN=0x80) clears only 0x00..0x7F, so the mirror survives into
main() until stack growth reaches it - see VLCD_EarlyInit() and
Timeslice_EarlyInit().

| idata   | content                     | nominal | source                     |
|---------|-----------------------------|---------|----------------------------|
| 0xEF/F0 | BGV, mV (bandgap)           | 1190    | 10.4.2 + reverse-VCC demo  |
| 0xF8/F9 | WKT clock, Hz               | 32768   | 7.11.1 + 10.4.3            |

The xdata CHIPID area (0xFDE0..) documents the same words but reads as
fixed fill on this chip (BGV bring-up finding) - always use the mirror.
*/

/**
HT1621 to LCD pinout
====================
COM0~COM3 and SEG0~SEG5 are used

LCD is a 3 digit number (first and second digit contains point)

driving voltage: ~3.2V RMS (bench-verified with chopped direct drive;
the original 1.3V DC assumption was disproven on this panel)
drive method: 1/4 duty 1/3 bias

| COM  | SEG  | USAGE |
|------|------|-------|
| COM0 | SEG0 | 1A    |
| COM0 | SEG1 | 1C    |
| COM0 | SEG2 | 2A    |
| COM0 | SEG3 | 2C    |
| COM0 | SEG4 | 3A    |
| COM0 | SEG5 | 3C    |
| COM1 | SEG0 | 1B    |
| COM1 | SEG1 | 1D    |
| COM1 | SEG2 | 2B    |
| COM1 | SEG3 | 2D    |
| COM1 | SEG4 | 3B    |
| COM1 | SEG5 | 3D    |
| COM2 | SEG0 | 1F    |
| COM2 | SEG1 | 1E    |
| COM2 | SEG2 | 2F    |
| COM2 | SEG3 | 2E    |
| COM2 | SEG4 | 3F    |
| COM2 | SEG5 | 3E    |
| COM3 | SEG0 | 1G    |
| COM3 | SEG1 | DP1   |
| COM3 | SEG2 | 2G    |
| COM3 | SEG3 | DP2   |
| COM3 | SEG4 | 3G    |
| COM3 | SEG5 | NC    |

*/

#endif
