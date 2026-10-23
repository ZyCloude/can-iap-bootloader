/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    can.h
  * @brief   This file contains all the function prototypes for
  *          the can.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __CAN_H__
#define __CAN_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern CAN_HandleTypeDef hcan1;

/* USER CODE BEGIN Private defines */

/* ===== CAN 应用层协议定义 (v1) ===== */

/* ★ 帧 ID 基址: 实际发送 ID = CAN_ID_BASE + 节点号  → 0x101 / 0x102 */
#define CAN_ID_BASE        0x100

/* ★ 功能码 (Data[0]) */
#define FUNC_HEARTBEAT     0x01      /* 心跳 */
#define FUNC_TEMP          0x02      /* 温度数据 */
#define FUNC_CMD           0x03      /* 控制命令 */
#define FUNC_ACK           0x04      /* 应答 */

/* ★ Data 字段的位置 (DLC = 8) */
#define PROTO_POS_FUNC     0         /* Data[0]     = 功能码 */
#define PROTO_POS_SEQ      1         /* Data[1]     = 序号 */
#define PROTO_POS_DATA     2         /* Data[2..6]  = 有效数据 (5 字节) */
#define PROTO_POS_CRC      7         /* Data[7]     = CRC8 校验 */

/* ★ CRC8 参数 (标准 CRC-8: 多项式 0x07, 初值 0x00, 不反转) */
#define PROTO_CRC8_POLY    0x07

/* USER CODE END Private defines */

void MX_CAN1_Init(void);

/* USER CODE BEGIN Prototypes */

typedef struct
{
    uint32_t id;        /* 帧 ID */
    uint8_t  dlc;       /* 数据长度 */
    uint8_t  data[8];   /* 数据 */
} CanFrame_t;           /* ★ 13 字节, 按 4 对齐 = 16 字节 */

/**
  * @brief  计算 CRC8 (标准 CRC-8: 多项式 0x07, 初值 0x00, 不反转)
  * @param  data : 数据指针
  * @param  len  : 数据长度 (字节数)
  * @retval CRC8 校验值
  */
uint8_t CAN_CalcCRC8(const uint8_t *data, uint8_t len);

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __CAN_H__ */
