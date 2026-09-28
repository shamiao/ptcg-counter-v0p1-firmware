#ifndef __BUTTONS_H__
#define __BUTTONS_H__

/* Buttons_Read() bitmask: 1 = pressed (pin pulled low). */

#define BUTTON_KEY_MAIN  0x01    /* P3.2 (INT0-capable, wake source)   */
#define BUTTON_KEY1      0x02    /* P3.3 */
#define BUTTON_KEY2      0x04    /* P3.4 */
#define BUTTON_KEY3      0x08    /* P3.5 */
#define BUTTON_KEY4      0x10    /* P3.6 */
#define BUTTON_KEY5      0x20    /* P3.7 */
#define BUTTON_KEY6      0x40    /* P1.0 */

void Buttons_Init(void);
unsigned char Buttons_Read(void);

#endif
