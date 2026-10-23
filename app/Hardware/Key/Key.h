#ifndef __KEY_H
#define __KEY_H

#include "main.h"

/* 按键 PF1~PF4(Key1~4)：输入+上拉，按下读到低电平(0) */
void Key_Init(void);                 /* 空：引脚已由 MX_GPIO_Init 配好 */
uint8_t Key_GetNum(void);            /* 0=无按键, 1~4=对应按键(带消抖、等松手) */

#endif
