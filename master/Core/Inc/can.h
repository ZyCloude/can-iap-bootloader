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

/* ★★★ 升级协议 v1 —— 功能码 0x10~0x13（与业务帧 0x01~0x04 分段，不撞） */
#define FUNC_UP_START   0x10    /* 开始升级 */
#define FUNC_UP_DATA    0x11    /* 固件数据 */
#define FUNC_UP_END     0x12    /* 传输结束 */
#define FUNC_UP_ACK     0x13    /* BootLoader 应答 */

/* ★★★ 升级协议 v1 帧格式（DLC = 8）
   ┌────────┬─────────────────┬────────┐
   │Data[0] │   Data[1..6]    │ Data[7]│
   │ 功能码 │   各帧不同       │  CRC8  │
   └────────┴─────────────────┴────────┘
   ★ 多字节字段一律【小端：低字节在前】

   功能码        Data[1..6]                                        Data[7]
   ────────────────────────────────────────────────────────────────────────
   START 0x10   [1..4] = 固件总字节数(32位)   [5..6] = 保留(填0)      CRC8
   DATA  0x11   [1..2] = 帧序号(16位)   [3..6] = 4 字节固件          CRC8
   END   0x12   [1..2] = 总帧数(16位)   [3..6] = 保留(填0)           CRC8
   ACK   0x13   [1] = 状态(0=OK)  [2..3] = 已收帧数   [4..6] = 保留   CRC8
   ────────────────────────────────────────────────────────────────────────
   ★ 【所有帧共有】: Data[0] 都是功能码;  Data[7] 都是 CRC8
        （CRC8 算的是【前 7 字节】: CAN_CalcCRC8(data, 7)）
   ★ 【保留字段】一律填 0 —— 便于将来扩展, 也让"填没填"一眼可查     */

#define UP_POS_SEQ      1       /* 序号起始位置 */
#define UP_POS_DATA     3       /* 固件数据起始位置（DATA 帧）*/
#define UP_DATA_LEN     4       /* 每帧 4 字节固件 = 正好一个 Flash WORD */

/* ★★★ ID rule: data frame = 0x100 + target node; ACK frame = 0x200 + responder node */
#define UP_ID_DATA_BASE   0x100U
#define UP_ID_ACK_BASE    0x200U

#define UP_TARGET_ID      1U       /* mother node: which node to upgrade (01) */

/* ★★★ 统一约定: 项目里所有"结果"都用这两个值（和 01机 一致）
       ★ 跟 C/HAL/POSIX 惯例: 0 = 成功, 非0 = 失败                    */
#define RES_OK      0U      /* 成功 */
#define RES_FAIL    1U      /* 失败 */

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
