#include <stc8h.h>
#include <stdio.h>

#include "hardware-definition.h"
#include "common.h"
#include "ht1621.h"

/* ---------------- HT1621B LCD driver (bit-bang) ----------------------
 * Wiring: /CS=P1.5, /WR=P1.6, DATA=P1.7, /RD=P1.2 (all push-pull)
 * Datasheet v3.40 AC limits: WR clock <= 150kHz, RD clock <= 75kHz
 *   -> the bit half-period below is 48 NOPs = 8us @6MHz 1T, plus a few
 *      cycles of pin/branch overhead per phase: measured-out RD clock
 *      lands near 57kHz and WR near 56kHz, both inside the limits.
 *      No other delays needed: data/CS setup+hold (60~600ns) are
 *      covered by the same period.
 * Wire bit order (per datasheet timing figures):
 *   mode ID "100"/"101" MSB-first; address A5..A0 MSB-first;
 *   command code C8..C1 MSB-first + 1 trailing don't-care;
 *   data nibbles MSB-first (D3..D0) - BENCH-VERIFIED: the datasheet
 *   figure labels them "D0 D1 D2 D3", but sending D0 first latches
 *   the nibble bit-reversed ('0' showed as bdefg); every field on
 *   this bus is MSB-first, matching classic HT1621 drivers.
 * RAM map: address n = SEG n (0..31), bits D0..D3 = COM0..COM3.
 * HT1621 powers up in SYS_DIS; init runs >50ms after VCC (after ADC
 * self-measure + banner prints), so no extra power-up wait is needed. */
#define HT1621_CS    P15
#define HT1621_WR    P16
#define HT1621_DATA  P17
#define HT1621_RD    P12

#define HT1621_PORT_MASK  0xE4    /* P1.2(/RD), P1.5(/CS), P1.6(/WR),
                                      P1.7(DATA) - all push-pull      */
/* Bit half-period: 48 NOPs = 8us @ 6MHz 1T. A function, not a macro,
 * so the NOPs exist once; LCALL/RET adds ~1us, RD clock ~52kHz < 75kHz. */
static void HT1621_BitDelay(void)
{
    NOP40(),NOP8();
}

void HT1621_Init(void)
{
    P1M0 |= HT1621_PORT_MASK;     /* bus pins push-pull (masked, keeps
                                     P1.0/P1.1/P1.3/P1.4 untouched)  */
    P1M1 &= ~HT1621_PORT_MASK;
    HT1621_CS = 1; HT1621_WR = 1; HT1621_RD = 1;   /* bus idle */
    HT1621_DATA = 0;
}

/* one WR-clocked bit; HT1621 latches DATA on the WR rising edge */
static void HT1621_SendWrBit(unsigned char b)
{
    HT1621_DATA = (b) ? 1 : 0;
    HT1621_BitDelay();       /* data setup since last WR rising edge */
    HT1621_WR  = 0;
    HT1621_BitDelay();       /* WR low phase */
    HT1621_WR  = 1;
}

/* command mode: "100" + code C8..C1 (MSB first) + 1 don't-care bit */
static void HT1621_SendCommand(unsigned char cmd)
{
    unsigned char i;
    HT1621_CS = 0;
    HT1621_SendWrBit(1);
    HT1621_SendWrBit(0);
    HT1621_SendWrBit(0);
    for (i = 0; i < 8; i++)
        HT1621_SendWrBit((cmd << i) & 0x80);
    HT1621_SendWrBit(0);          /* trailing don't-care bit */
    HT1621_BitDelay();       /* CS hold before rising */
    HT1621_CS = 1;
}

/* system bring-up: idle the bus lines, then SYS_EN + on-chip 256kHz
 * RC oscillator. Both the TONE (buzzer) generator and the LCD driver
 * run from that oscillator, so this must run BEFORE the beep test.  */
void HT1621_SysInit(void)
{
    HT1621_CS = 1; HT1621_WR = 1; HT1621_RD = 1; HT1621_DATA = 0;

    HT1621_SendCommand(0x01);     /* SYS_EN        0000-0001-X */
    HT1621_SendCommand(0x18);     /* RC_256K osc   0001-10XX-X */
}

/* normal display bring-up (not the all-on diagnostic): LCD bias and
 * drive on. Panel is 1/3 bias, 4 COM.                               */
void HT1621_LcdOn(void)
{
    HT1621_SendCommand(0x29);     /* BIAS 1/3, 4COM 0010-10X1-X */
    HT1621_SendCommand(0x03);     /* LCD_ON        0000-0011-X */
}

/* display park (standby path): drive off; caller follows with
 * SysDisable to stop the oscillator entirely                        */
void HT1621_LcdOff(void)
{
    HT1621_SendCommand(0x02);     /* LCD_OFF       0000-0010-X */
}

/* full chip park: stops the on-chip RC oscillator - panel, tone and
 * RAM addressing are all dead until the next SysInit (RAM content
 * is not retained through this)                                    */
void HT1621_SysDisable(void)
{
    HT1621_SendCommand(0x00);     /* SYS_DIS       0000-0000-X */
}

/* buzzer primitives: the tone generator runs from the same RC_256K
 * oscillator as the LCD driver, so SysInit must precede any TONE.  */
void HT1621_Buzzer2kOn(void)
{
    HT1621_SendCommand(0x60);     /* TONE 2K  011X-XXXX-X */
    HT1621_SendCommand(0x09);     /* TONE ON  0000-1001-X */
}

void HT1621_BuzzerOff(void)
{
    HT1621_SendCommand(0x08);     /* TONE OFF 0000-1000-X */
}

/* WRITE burst: mode ID "101", address A5..A0 MSB-first, then n data
 * nibbles MSB-first (D3..D0); the address auto-increments per nibble. */
void HT1621_WriteRam(unsigned char addr, unsigned char *src, unsigned char n)
{
    unsigned char i;

    HT1621_CS = 0;
    HT1621_SendWrBit(1);
    HT1621_SendWrBit(0);
    HT1621_SendWrBit(1);               /* mode ID "101" = WRITE     */
    for (i = 0; i < 6; i++)            /* address A5..A0            */
        HT1621_SendWrBit((addr << i) & 0x20);
    while (n--)
    {
        for (i = 0; i < 4; i++)        /* D3..D0, MSB first         */
            HT1621_SendWrBit((*src >> (3 - i)) & 1);
        src++;
    }
    HT1621_BitDelay();                 /* CS hold                   */
    HT1621_CS = 1;
}

/* display test (healthy path only, below the overvoltage fence):
 * BIAS + LCD_ON, then light every segment via READ-MODIFY-WRITE     */
void HT1621_AllSegmentsOn(void)
{
    unsigned char a, i, rd;
    unsigned char xdata rdv[6];     /* debug: RMW report buffer */

    HT1621_SendCommand(0x29);     /* BIAS 1/3, 4COM 0010-10X1-X */
    printf("HT1621 CMD BIAS 1/3, 4COM  0x29\r\n");
    HT1621_SendCommand(0x03);     /* LCD_ON        0000-0011-X */
    printf("HT1621 CMD LCD_ON          0x03\r\n");

    /* READ-MODIFY-WRITE (mode ID 101), successive addresses SEG0..SEG5:
     * per address: 4 bits clocked out by /RD, 4 bits clocked in by /WR,
     * address auto-advances after the write. CS stays low throughout.  */
    HT1621_CS = 0;
    HT1621_SendWrBit(1);
    HT1621_SendWrBit(0);
    HT1621_SendWrBit(1);              /* mode ID "101" */
    for (i = 0; i < 6; i++)           /* address A5..A0 = 000000 (SEG0) */
        HT1621_SendWrBit(0);

    for (a = 0; a < 6; a++)
    {
        rd = 0;
        P1M0 &= ~0x80; P1M1 |= 0x80;  /* P1.7 -> high-Z: HT1621 drives DATA */
        for (i = 0; i < 4; i++)       /* read D3..D0, /RD clocks          */
        {
            HT1621_RD = 0;
            HT1621_BitDelay();   /* RD low phase + data valid */
            rd = (rd << 1) | (HT1621_DATA ? 1 : 0);
            HT1621_RD = 1;
            HT1621_BitDelay();   /* RD high phase */
        }
        P1M1 &= ~0x80; P1M0 |= 0x80;  /* P1.7 back to push-pull output */
        rdv[a] = rd;
        for (i = 0; i < 4; i++)       /* write D3..D0, OR all-on */
            HT1621_SendWrBit(((rd | 0x0F) >> (3 - i)) & 1);
    }
    HT1621_BitDelay();           /* CS hold */
    HT1621_CS = 1;                    /* end RMW session */

    for (a = 0; a < 6; a++)
        printf("HT1621 RMW SEG%bu: read=0x%02bX -> write=0x%02bX\r\n",
               a, rdv[a], (unsigned char)(rdv[a] | 0x0F));
    printf("HT1621: all segments on\r\n");
}

/* Diag 4: buzzer test. TONE output needs the system oscillator
 * running, so an audible beep also proves SYS_EN + RC_256K were really
 * decoded and executed (RAM read/write only proves the data path).
 * Requires a buzzer element on the BZ/BZBAR pins to be audible.   */
void HT1621_BeepTest(void)
{
    HT1621_SendCommand(0x40);     /* TONE 4K  010X-XXXX-X */
    printf("HT1621 CMD TONE_4K        0x40\r\n");
    HT1621_SendCommand(0x09);     /* TONE ON  0000-1001-X */
    printf("HT1621 CMD TONE_ON        0x09  -> beep 4kHz 0.2s\r\n");
    DelayMs(20);
    HT1621_SendCommand(0x08);     /* TONE OFF 0000-1000-X */
    DelayMs(300);

    HT1621_SendCommand(0x60);     /* TONE 2K  011X-XXXX-X */
    printf("HT1621 CMD TONE_2K        0x60\r\n");
    HT1621_SendCommand(0x09);     /* TONE ON */
    printf("HT1621 CMD TONE_ON        0x09  -> beep 2kHz 0.2s\r\n");
    DelayMs(20);
    HT1621_SendCommand(0x08);     /* TONE OFF */
    printf("HT1621 CMD TONE_OFF       0x08  (buzzer test done)\r\n");
}

/* ---- pure-read diagnostics ---- */

/* Diag 1: detect an external pull-up on the DATA line.
 * All HT1621 lines idle (CS/WR/RD high), P1.7 pre-charged low then
 * released (high-Z). A pull-up drifts the pin to 1 within ms;
 * a bare line stays 0. If a pull-up exists, "read=0x0F" could be a
 * floating-input artifact instead of real chip output.            */
void HT1621_DataFloatTest(void)
{
    unsigned char xdata v1, v2;     /* debug: readings for the report */
    HT1621_CS = 1; HT1621_WR = 1; HT1621_RD = 1;
    HT1621_DATA = 0;                 /* pre-charge line low */
    P1M0 &= ~0x80; P1M1 |= 0x80;     /* P1.7 -> high-Z     */
    DelayMs(5); v1 = HT1621_DATA;
    DelayMs(5); v2 = HT1621_DATA;
    P1M1 &= ~0x80; P1M0 |= 0x80;     /* back to push-pull  */
    printf("HT1621 DATA float test: %bu then %bu\r\n", v1, v2);
    printf("  -> 1 means pull-up present (0x0F reads may be fake)\r\n");
}

/* Diag 2: READ mode ("110"), dump all 32 SEG nibbles without writing.
 * SEG0..SEG5 should read 0x0F if the RMW writes really landed;
 * SEG6..SEG31 show the untouched power-on RAM content.           */
void HT1621_ReadDump(void)
{
    unsigned char seg, i, rd;
    unsigned char xdata buf[32];    /* debug: SEG dump buffer */

    HT1621_CS = 0;
    HT1621_SendWrBit(1);
    HT1621_SendWrBit(1);
    HT1621_SendWrBit(0);             /* mode ID "110" = READ */
    for (i = 0; i < 6; i++)          /* address A5..A0 = 0   */
        HT1621_SendWrBit(0);

    P1M0 &= ~0x80; P1M1 |= 0x80;     /* P1.7 high-Z: chip drives */
    for (seg = 0; seg < 32; seg++)
    {
        rd = 0;
        for (i = 0; i < 4; i++)     /* D3..D0, /RD clocks       */
        {
            HT1621_RD = 0;
            HT1621_BitDelay();
            rd = (rd << 1) | (HT1621_DATA ? 1 : 0);
            HT1621_RD = 1;
            HT1621_BitDelay();
        }
        buf[seg] = rd;
    }
    P1M1 &= ~0x80; P1M0 |= 0x80;     /* back to push-pull */
    HT1621_BitDelay();
    HT1621_CS = 1;

    for (seg = 0; seg < 32; seg += 2)
        printf("SEG%02bu,%02bu: %02bX %02bX\r\n",
               seg, (unsigned char)(seg + 1), buf[seg], buf[seg + 1]);
    printf("  -> SEG0-5 should be 0F if writes landed\r\n");
}
