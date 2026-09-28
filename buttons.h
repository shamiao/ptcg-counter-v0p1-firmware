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

/* Key FSM, stepped once per 20ms slice. Returns the mask of a newly
 * accepted single-key press (the exactly-one-key edge), 0 otherwise.
 * States: S0 idle, S1 one key held (already fired, no auto-repeat),
 * S2 multi-key block (2+ keys = invalid, exits only on full release).
 * Boot always starts in S0: a key held through power-up fires once at
 * the first scan. Debounce = the sampling cadence itself (contact
 * bounce shorter than one slice never lands on a sample).           */
unsigned char Buttons_Scan(void);

/* KEY0 (KEY_MAIN) release tracker - the second, orthogonal machine.
 * Armed on the accepted KEY0 press edge (inside Buttons_Scan), it
 * fires exactly once when KEY0 releases - both on the normal S1->S0
 * release AND inside the S2 multi-key block (KEY0 released while
 * other keys are still held; the main FSM stays blocked in S2 until
 * everything is out). A KEY0 press that was never accepted (e.g. it
 * joined as the second key of a multi-press) never arms the tracker.
 * Call once per slice, after Buttons_Scan.                           */
unsigned char Buttons_Key0Released(void);

#endif
