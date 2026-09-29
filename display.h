#ifndef __DISPLAY_H__
#define __DISPLAY_H__

/* 3-digit segment display buffer, rendered to the HT1621 by
 * Display_Render().
 *   g_disp_buf[0]=units, [1]=tens, [2]=hundreds (right to left)
 *   bits 0-5: char code 0..35 = '0'..'9','A'..'Z'; >35 renders blank
 *   bit 6: decimal point (no DP on the units digit - flag ignored)
 *   bit 7: enable (0 = glyph dark; the DP flag still applies)        */

#define DISP_EN  0x80
#define DISP_DP  0x40

/* char code for bits 0-5: '0'..'9' -> 0..9, 'A'..'Z' -> 10..35        */
#define DISP_GLYPH(ch)  ((ch) <= '9' ? ((ch) - '0') : ((ch) - 'A' + 10))

extern unsigned char data g_disp_buf[3];

void Display_Render(void);   /* full rewrite of HT1621 SEG0..SEG5 */

#endif
