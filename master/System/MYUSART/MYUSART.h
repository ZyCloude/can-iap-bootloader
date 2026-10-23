#ifndef __MYUSART_H
#define __MYUSART_H

#include "main.h"      /* brings in HAL types; huart1 declared extern in USART.c */

/* ---- Send API ---- */
void Serial_SendByte(uint8_t Byte);                         /* send one byte */
void Serial_SendArray(uint8_t *Array, uint16_t Length);     /* send an array */
void Serial_SendString(char *String);                       /* send a C string (0-terminated) */
void Serial_SendNumber(uint32_t Number, uint8_t Length);    /* send a decimal number */
void Serial_Printf(char *format, ...);                      /* formatted print via USART1 */
void Serial_SendPacket(void);                               /* 0xFF head + 4 bytes + 0xFE tail */

/* ---- Receive flag (used when RX interrupt is enabled later) ---- */
uint8_t Serial_GetRxFlag(void);                             /* returns 1 if a packet was received (auto clears) */

/* ---- Packet buffers ---- */
extern uint8_t Serial_TxPacket[5];
extern uint8_t Serial_RxPacket[5];
extern uint8_t Serial_RxFlag;

#endif
