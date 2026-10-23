/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    can.c
  * @brief   This file provides code for the configuration
  *          of the CAN instances.
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
/* Includes ------------------------------------------------------------------*/
#include "can.h"

/* USER CODE BEGIN 0 */
#include <string.h>                /* ★ memcpy (回调里还要用) */

/* ★★★ 环形缓冲的实体(类型声明在 can.h) —— 替代原来的 RTOS 队列 */
CanRing_t g_canRb;
/* USER CODE END 0 */

CAN_HandleTypeDef hcan1;

/* CAN1 init function */
void MX_CAN1_Init(void)
{

  /* USER CODE BEGIN CAN1_Init 0 */

  /* USER CODE END CAN1_Init 0 */

  /* USER CODE BEGIN CAN1_Init 1 */

  /* USER CODE END CAN1_Init 1 */
  hcan1.Instance = CAN1;
  hcan1.Init.Prescaler = 6;
  hcan1.Init.Mode = CAN_MODE_NORMAL;
  hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan1.Init.TimeSeg1 = CAN_BS1_11TQ;
  hcan1.Init.TimeSeg2 = CAN_BS2_2TQ;
  hcan1.Init.TimeTriggeredMode = DISABLE;
  hcan1.Init.AutoBusOff = ENABLE;
  hcan1.Init.AutoWakeUp = DISABLE;
  hcan1.Init.AutoRetransmission = ENABLE;
  hcan1.Init.ReceiveFifoLocked = DISABLE;
  hcan1.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN1_Init 2 */

 CAN_FilterTypeDef sFilterConfig;                              /* 过滤器配置结构体 */

  /* ---- ★ 下面这些字段, 正好对应你刚学的"掩码理论" ---- */
                               /* 用第 0 号过滤器组 */
  sFilterConfig.FilterMode           = CAN_FILTERMODE_IDMASK;   /* ★ 掩码模式(不是列表模式) */
  sFilterConfig.FilterScale          = CAN_FILTERSCALE_32BIT;   /* 32 位宽 */
  sFilterConfig.FilterIdHigh         = 0x0000;                  /* ★ 期望 ID: 全 0 */
  sFilterConfig.FilterIdLow          = 0x0000;
  sFilterConfig.FilterMaskIdHigh     = 0x0000;                  /* ★ 掩码: 全 0 */
  sFilterConfig.FilterMaskIdLow      = 0x0000;                  /*   => 一个位都不检查 => 全通过 */
  sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;            /* 通过的帧进 FIFO0 */
  sFilterConfig.FilterActivation     = ENABLE;                  /* ★★★ 必须激活! */
  sFilterConfig.SlaveStartFilterBank = 14;                      /* 单 CAN 用不到 */

  if (HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig) != HAL_OK)   /* 把过滤器写进硬件 */
  {
    Error_Handler();
  }

  HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING);

  if (HAL_CAN_Start(&hcan1) != HAL_OK)                          /* ★★★ 启动 CAN */
  {
    Error_Handler();
//    hcan1.ErrorCode = HAL_CAN_ERROR_NONE;          /* 清错误码 */
//    hcan1.State     = HAL_CAN_STATE_LISTENING;     /* 假装"已启动" */
  }

  /* USER CODE END CAN1_Init 2 */

}

void HAL_CAN_MspInit(CAN_HandleTypeDef* canHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(canHandle->Instance==CAN1)
  {
  /* USER CODE BEGIN CAN1_MspInit 0 */

  /* USER CODE END CAN1_MspInit 0 */
    /* CAN1 clock enable */
    __HAL_RCC_CAN1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**CAN1 GPIO Configuration
    PA11     ------> CAN1_RX
    PA12     ------> CAN1_TX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_CAN1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_CAN1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* CAN1 interrupt Init */
    HAL_NVIC_SetPriority(CAN1_RX0_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);
  /* USER CODE BEGIN CAN1_MspInit 1 */

  /* USER CODE END CAN1_MspInit 1 */
  }
}

void HAL_CAN_MspDeInit(CAN_HandleTypeDef* canHandle)
{

  if(canHandle->Instance==CAN1)
  {
  /* USER CODE BEGIN CAN1_MspDeInit 0 */

  /* USER CODE END CAN1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_CAN1_CLK_DISABLE();

    /**CAN1 GPIO Configuration
    PA11     ------> CAN1_RX
    PA12     ------> CAN1_TX
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11|GPIO_PIN_12);

    /* CAN1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(CAN1_RX0_IRQn);
  /* USER CODE BEGIN CAN1_MspDeInit 1 */

  /* USER CODE END CAN1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* ★ 主循环取帧: 返回 1=取到一帧, 0=缓冲空 */
uint8_t CanRing_Get(CanFrame_t *out)
{
    uint16_t next;

    /* ★ 空? (tail 追上 head = 没货了) */
    if (g_canRb.tail == g_canRb.head)
        return RES_FAIL;        /* ★ 空 = 失败 (全项目统一: 0=成功, 1=失败) */

    /* ★★ 取走这一帧 */
    *out = g_canRb.buf[g_canRb.tail];

    /* ★★★ tail 往后挪一格 (到头就绕回) —— 和回调里"算 next"一个套路 */
    if (g_canRb.tail >= (CAN_RB_SIZE - 1U))
        next = 0U;
    else
        next = g_canRb.tail + 1U;

    g_canRb.tail = next;
    return RES_OK;              /* ★ 取到 = 成功 (全项目统一: 0=成功) */
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef RxHeader;
    uint8_t    RxData[8];
    CanFrame_t frame;
    uint16_t   next;                                /* ★ 新增这一行 */

    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &RxHeader, RxData) != HAL_OK)
        return;

    frame.id  = (RxHeader.IDE == CAN_ID_STD) ? RxHeader.StdId : RxHeader.ExtId;
    frame.dlc = RxHeader.DLC;
    memcpy(frame.data, RxData, 8);

    /* ★★★ 换成: 写进环形缓冲 (满了就丢; 丢的帧靠协议层"序号+重传"补) */

    /* ★★★ 第 1 步: 算出"head 往后挪一格, 挪到哪" */
    if (g_canRb.head >= (CAN_RB_SIZE - 1U))     /* 已经在最后一格了? */
        next = 0U;                              /* → 绕回开头 */
    else
        next = g_canRb.head + 1U;               /* → 否则就往后挪一格 */

    /* ★★ 第 2 步: 那一格空着吗? (空 = 没撞上 tail) */
    if (next != g_canRb.tail)
    {
        g_canRb.buf[g_canRb.head] = frame;      /* 写进去 */
        g_canRb.head = next;                    /* 指针挪一格 */
    }
    /* ★ 撞上了 = 满了 → 不写 = 丢掉这帧 (中断里不能等) */

}

/**
  * @brief  计算 CRC8 (标准 CRC-8: 多项式 0x07, 初值 0x00, 不反转)
  * @param  data : 数据指针
  * @param  len  : 数据长度 (字节数)
  * @retval CRC8 校验值
  */
uint8_t CAN_CalcCRC8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0x00;          /* ★ 初值 */
    uint8_t i, bit;

    for (i = 0; i < len; i++)
    {
        crc ^= data[i];          /* ★ 把当前字节"异或"进来 */

        for (bit = 0; bit < 8; bit++)
        {
            if (crc & 0x80)      /* ★ 看最高位是不是 1 */
            {
                crc = (uint8_t)((crc << 1) ^ PROTO_CRC8_POLY);   /* ★ 是 1 → 左移后异或多项式 */
            }
            else
            {
                crc = (uint8_t)(crc << 1);                       /* ★ 是 0 → 只左移 */
            }
        }
    }

    return crc;
}

/* USER CODE END 1 */
