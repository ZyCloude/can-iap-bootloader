#include "MYUSART.h"
#include <stdio.h>
#include <stdarg.h>
#include "main.h"

/* UART1 handle: defined in main.c, initialized by CubeMX MX_USART1_UART_Init() */
extern UART_HandleTypeDef huart1;

/* ---- Packet buffers ---- */
uint8_t Serial_TxPacket[5];
uint8_t Serial_RxPacket[5];
uint8_t Serial_RxFlag;

/* ---- printf redirect to USART1 (avoid semihosting) ---- */
#if 1
#pragma import(__use_no_semihosting)
struct __FILE { int handle; };
FILE __stdout;
void _sys_exit(int x) { x = x; }
int fputc(int ch, FILE *f)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 100);
    return ch;
}
#endif

/* ---- Send functions ---- */
void Serial_SendByte(uint8_t Byte)
{
    HAL_UART_Transmit(&huart1, &Byte, 1, 100);
}

void Serial_SendArray(uint8_t *Array, uint16_t Length)
{
    HAL_UART_Transmit(&huart1, Array, Length, 100);
}

void Serial_SendString(char *String)
{
    while(*String != '\0')
    {
        Serial_SendByte((uint8_t)*String++);
    }
}

static uint32_t Serial_Pow(uint32_t X, uint32_t Y)
{
    uint32_t Result = 1;
    while(Y--) { Result *= X; }
    return Result;
}

void Serial_SendNumber(uint32_t Number, uint8_t Length)
{
    uint8_t i;
    for(i = 0; i < Length; i++)
    {
        Serial_SendByte(Number / Serial_Pow(10, Length - i - 1) % 10 + '0');
    }
}

void Serial_Printf(char *format, ...)
{
    char String[100];
    va_list arg;
    va_start(arg, format);
    vsprintf(String, format, arg);
    va_end(arg);
    Serial_SendString(String);
}

void Serial_SendPacket(void)
{
    Serial_SendByte(0xFF);
    Serial_SendArray(Serial_TxPacket, 4);
    Serial_SendByte(0xFE);
}

uint8_t Serial_GetRxFlag(void)
{
    if(Serial_RxFlag == 1)
    {
        Serial_RxFlag = 0;
        return 1;
    }
    return 0;
}
