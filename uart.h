#ifndef __UART_H__
#define __UART_H__

/* UART1 on P3.0/P3.1, 8N1, baud from Timer1 mode-0 16-bit auto-reload
 * (1T), polling TX. Baud rate: UART_BAUDRATE (hardware-definition.h). */

void UART1_Init(void);
void UART1_SendByte(unsigned char dat);

#endif
