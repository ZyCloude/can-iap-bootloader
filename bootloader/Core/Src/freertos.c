/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "can.h"        /* ★ 提供 hcan1 的声明 */
#include "MYUSART.h"    /* ★ 提供 Serial_Printf —— ★ 函数名按你 MYUSART.h 里实际的来 */
#include "usart.h"        /* 用 huart1 */

extern CAN_HandleTypeDef hcan1;   /* ★ 引用 can.c 里定义的那个 CAN 句柄 */



/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define NODE_ID   1

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

osMessageQueueId_t CanRxQueueHandle = NULL;     /* CAN 接收队列句柄 */

osMessageQueueId_t CanProcQueueHandle = NULL;   /* 处理队列句柄 (CanRxTask → CanProcessTask) */

osMessageQueueId_t UartRxQueueHandle   = NULL;   /* 串口接收队列: 中断回调 → UartRxTask */

osMessageQueueId_t UartProcQueueHandle = NULL;   /* 串口处理队列: UartRxTask → UartProcTask */
/* ★★★ 新增: 统计计数器 (volatile: 会被别的任务改写) */
volatile uint32_t g_RxFrameCount = 0;           /* 收到并通过 CRC8 的帧数 */

/* ★★★ 新增: 节点监控 —— 每个节点一份统计 (按 ID 索引) */
#define NODE_IDX_MAX  8                         /* ★ 支持 0x100 ~ 0x107 */

typedef struct {
	uint32_t rxCount;         /* 该节点发来的帧数 */
	uint8_t  lastSeq;         /* 上次收到的序号 */
	uint32_t lostCount;       /* 丢帧数 */
	uint32_t lastTick;        /* ★★★ 新增: 上次收到该节点帧的时刻(ms) */
	uint8_t  online;          /* ★★★ 新增: 在线标志 (0=离线, 1=在线) */
} NodeStat_t;

volatile NodeStat_t g_NodeStat[NODE_IDX_MAX];   /* ★ 索引 = frame.id - CAN_ID_BASE */

volatile uint32_t g_uartRxCount = 0;      /* 累计收到字节数, 跨任务读写所以 volatile */

volatile uint32_t g_uartDropLine = 0;	//"丢行计数"

/* ★★★ 新增: 一整行 —— UartRxTask 攒好一行后, 按这个结构投给 UartProcTask */
#define UART_LINE_MAX  128          /* 一行最多几个字节 */

typedef struct {
    uint16_t len;                   /* ★ 这一行实际有多少字节 */
    uint8_t  data[UART_LINE_MAX];   /* 内容 */
} UartLine_t;

/* USER CODE END Variables */
/* Definitions for CanRxTask */
osThreadId_t CanRxTaskHandle;
const osThreadAttr_t CanRxTask_attributes = {
  .name = "CanRxTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for CanProcessTask */
osThreadId_t CanProcessTaskHandle;
const osThreadAttr_t CanProcessTask_attributes = {
  .name = "CanProcessTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for CanTxTask */
osThreadId_t CanTxTaskHandle;
const osThreadAttr_t CanTxTask_attributes = {
  .name = "CanTxTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for MonitorTask */
osThreadId_t MonitorTaskHandle;
const osThreadAttr_t MonitorTask_attributes = {
  .name = "MonitorTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for UartRxTask */
osThreadId_t UartRxTaskHandle;
const osThreadAttr_t UartRxTask_attributes = {
  .name = "UartRxTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for UartProcTask */
osThreadId_t UartProcTaskHandle;
const osThreadAttr_t UartProcTask_attributes = {
  .name = "UartProcTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartCanRxTask(void *argument);
void StartCanProcessTask(void *argument);
void StartCanTxTask(void *argument);
void StartMonitorTask(void *argument);
void StartUartRxTask(void *argument);
void StartUartProcTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */

  /* ★★★ 自己建 CAN 接收队列 (不用 CubeMX 生成的, 免得 Item Size 不对) */
  {
      static const osMessageQueueAttr_t CanRxQueue_attr = { .name = "CanRxQueue" };
      CanRxQueueHandle = osMessageQueueNew(16,                   /* 16 条 */
                                           sizeof(CanFrame_t),   /* ★ 每条 = 16 字节 ? */
                                           &CanRxQueue_attr);
      if (CanRxQueueHandle == NULL)
      {
          Error_Handler();
      }
  }

  /* ★★★ 再建一个 CAN 处理队列 (CanRxTask → CanProcessTask) */
  {
      static const osMessageQueueAttr_t CanProcQueue_attr = { .name = "CanProcQueue" };
      CanProcQueueHandle = osMessageQueueNew(16,                   /* 16 条 */
                                             sizeof(CanFrame_t),   /* ★ 每条 = 16 字节 */
                                             &CanProcQueue_attr);
      if (CanProcQueueHandle == NULL)
      {
          Error_Handler();
      }
  }

 /* ★★★ 新增: 串口接收队列 */
  {
      static const osMessageQueueAttr_t UartRxQueue_attr = { .name = "UartRxQueue" };
      UartRxQueueHandle = osMessageQueueNew(64,                /* 64 条 */
                                            sizeof(uint8_t),   /* ★ 每条 1 字节 */
                                            &UartRxQueue_attr);
      if (UartRxQueueHandle == NULL)
      {
          Error_Handler();
      }
  }

  /* ★★★ 新增: 串口处理队列 */
  {
      static const osMessageQueueAttr_t UartProcQueue_attr = { .name = "UartProcQueue" };
      UartProcQueueHandle = osMessageQueueNew(4,                   /* ★ 4 条 */
                                              sizeof(UartLine_t),  /* ★ 每条 = 一整行 */
                                              &UartProcQueue_attr);
      if (UartProcQueueHandle == NULL)
      {
          Error_Handler();
      }
  }

  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
//  /* creation of CanRxTask */
//  CanRxTaskHandle = osThreadNew(StartCanRxTask, NULL, &CanRxTask_attributes);

//  /* creation of CanProcessTask */
//  CanProcessTaskHandle = osThreadNew(StartCanProcessTask, NULL, &CanProcessTask_attributes);

//  /* creation of CanTxTask */
//  CanTxTaskHandle = osThreadNew(StartCanTxTask, NULL, &CanTxTask_attributes);

//  /* creation of MonitorTask */
//  MonitorTaskHandle = osThreadNew(StartMonitorTask, NULL, &MonitorTask_attributes);

//  /* creation of UartRxTask */
//  UartRxTaskHandle = osThreadNew(StartUartRxTask, NULL, &UartRxTask_attributes);

//  /* creation of UartProcTask */
//  UartProcTaskHandle = osThreadNew(StartUartProcTask, NULL, &UartProcTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartCanRxTask */
/**
  * @brief  Function implementing the CanRxTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartCanRxTask */
void StartCanRxTask(void *argument)
{
  /* USER CODE BEGIN StartCanRxTask */
  CanFrame_t frame;                    /* ★ 每次循环复用这块局部内存 */

  for(;;)
  {
    /* ★ 阻塞等 CanRxQueue (最长 1000ms) —— 没帧就睡着, 不占 CPU */
    if (osMessageQueueGet(CanRxQueueHandle, &frame, NULL, 1000) == osOK)
    {
      /* ★★ 校验 CRC8: 整帧 8 字节算一遍, 应该 = 0 */
      if (CAN_CalcCRC8(frame.data, 8) == 0)
      {

	    g_RxFrameCount++;              /* ★★★ 新增: 只统计通过校验的帧 */

        /* ★★★ 校验通过 → 投到处理队列
           (★ 这里是【任务】不是中断, 用普通版 osMessageQueuePut 就行) */
        osMessageQueuePut(CanProcQueueHandle, &frame, 0, 0);
      }
      /* ★★★★ 校验失败 → 直接丢弃, 不往后续传坏数据 */
    }
  }
  /* USER CODE END StartCanRxTask */
}

/* USER CODE BEGIN Header_StartCanProcessTask */
/**
* @brief Function implementing the CanProcessTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartCanProcessTask */
void StartCanProcessTask(void *argument)
{
  /* USER CODE BEGIN StartCanProcessTask */
 CanFrame_t frame;

  for(;;)
  {
    /* ★ 阻塞等 CanProcQueue (★ CRC8 已在 CanRxTask 校验过) */
    if (osMessageQueueGet(CanProcQueueHandle, &frame, NULL, 1000) == osOK)
    {
      uint8_t func  = frame.data[PROTO_POS_FUNC];      /* 功能码 */
      uint8_t rxSeq = frame.data[PROTO_POS_SEQ];       /* 序号 */

      /* ★★ 按功能码分发 */
      switch (func)
      {
        case FUNC_HEARTBEAT:                            /* ★ 心跳 */
		{
			/* ★★★ 按节点独立计数 + 丢帧检测 (0x102 → 索引 2) */
			uint8_t idx = (uint8_t)(frame.id - CAN_ID_BASE);
			if (idx < NODE_IDX_MAX)
			{
				volatile NodeStat_t *st = &g_NodeStat[idx];

				/* ★★★ 丢帧检测: 序号必须连续递增 */
				if (st->rxCount > 0)                          /* ★ 不是第一帧才检查 */
				{
					uint8_t expect = (uint8_t)(st->lastSeq + 1);   /* ★ 期望的序号 */
					if (rxSeq != expect)                           /* ★ 跳号了! */
					{
						st->lostCount++;                           /* ★ 记一次丢帧 */
					}
				}
				st->lastSeq = rxSeq;                           /* ★ 记住本次序号 */
				st->rxCount++;
				st->lastTick = osKernelGetTickCount();         /* ★★★ 新增: 刷新"最后收到时刻" */

			}

			Serial_Printf("★ [心跳] ID=0x%03X 序号=%d\r\n", frame.id, rxSeq);
		}
          break;

        case FUNC_TEMP:                                 /* ★ 温度 (示例, 以后用) */
          Serial_Printf("★ [温度] ID=0x%03X 序号=%d 值=%d.%d C\r\n",
                        frame.id, rxSeq,
                        frame.data[PROTO_POS_DATA + 0],
                        frame.data[PROTO_POS_DATA + 1]);
          break;

        case FUNC_CMD:                                  /* ★ 命令 (示例, 以后用) */
          Serial_Printf("★ [命令] ID=0x%03X 序号=%d 命令码=0x%02X\r\n",
                        frame.id, rxSeq, frame.data[PROTO_POS_DATA + 0]);
          break;

        default:                                        /* ★ 未知功能码 */
          Serial_Printf("! 未知功能码 0x%02X (ID=0x%03X)\r\n", func, frame.id);
          break;
      }
    }
  }
  /* USER CODE END StartCanProcessTask */
}

/* USER CODE BEGIN Header_StartCanTxTask */
/**
* @brief Function implementing the CanTxTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartCanTxTask */
void StartCanTxTask(void *argument)
{
  /* USER CODE BEGIN StartCanTxTask */

  CAN_TxHeaderTypeDef TxHeader;
  uint8_t  TxData[8];
  uint32_t TxMailbox;
  uint8_t  seq = 0;                    /* ★ 发送序号 (本任务私有) */

  for(;;)
  {
    /* ===== 1. 按协议组装 Data[8] ===== */
    TxData[PROTO_POS_FUNC] = FUNC_HEARTBEAT;      /* Data[0] = 功能码 (心跳) */
    TxData[PROTO_POS_SEQ]  = seq++;               /* Data[1] = 序号, 每轮 +1 (自动回绕) */

    TxData[PROTO_POS_DATA + 0] = 0x11;            /* Data[2..6] = 有效数据 (5 字节) */
    TxData[PROTO_POS_DATA + 1] = 0x22;
    TxData[PROTO_POS_DATA + 2] = 0x33;
    TxData[PROTO_POS_DATA + 3] = 0x44;
    TxData[PROTO_POS_DATA + 4] = 0xEE;

    TxData[PROTO_POS_CRC] = CAN_CalcCRC8(TxData, PROTO_POS_CRC);   /* Data[7] = CRC8 */

    /* ===== 2. 填帧头 ===== */
    TxHeader.StdId = CAN_ID_BASE + NODE_ID;       /* ★ 0x101 (01机) / 0x102 (02机) */
    TxHeader.ExtId = 0;
    TxHeader.IDE   = CAN_ID_STD;
    TxHeader.RTR   = CAN_RTR_DATA;
    TxHeader.DLC   = 8;
    TxHeader.TransmitGlobalTime = DISABLE;

    /* ===== 3. 发出去 ===== */
    HAL_CAN_AddTxMessage(&hcan1, &TxHeader, TxData, &TxMailbox);

    /* ===== 4. 等 100ms 再发下一帧 ===== */
    osDelay(100);
  }

  /* USER CODE END StartCanTxTask */
}

/* USER CODE BEGIN Header_StartMonitorTask */
/**
* @brief Function implementing the MonitorTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartMonitorTask */
void StartMonitorTask(void *argument)
{
  /* USER CODE BEGIN StartMonitorTask */
  /* Infinite loop */
  for(;;)
  {

	/* ★★★ 每 1 秒打印一次统计 */
	Serial_Printf("★ [统计] 收帧总数=%u\r\n", (unsigned int)g_RxFrameCount);

	/* ★★★ 新增: 各任务栈的剩余空间 (★ 单位: 字节, 不用强转!) */
	Serial_Printf("[栈] Rx=%u Proc=%u Tx=%u Mon=%u\r\n",
	(unsigned int)osThreadGetStackSpace(CanRxTaskHandle),
	(unsigned int)osThreadGetStackSpace(CanProcessTaskHandle),
	(unsigned int)osThreadGetStackSpace(CanTxTaskHandle),
	(unsigned int)osThreadGetStackSpace(MonitorTaskHandle));

	/* ★★★ 新增: 各节点收帧数 (★ 单行打印, 不管几个节点都只占一行) */
	Serial_Printf("[节点] ");
	for (uint8_t i = 1; i < NODE_IDX_MAX; i++)
	{
		if (g_NodeStat[i].rxCount > 0)
		{
			Serial_Printf("0x%03X=%u(丢%u) ", (unsigned int)(CAN_ID_BASE + i),
										   (unsigned int)g_NodeStat[i].rxCount,
										   (unsigned int)g_NodeStat[i].lostCount);
		}
	}

	Serial_Printf("\r\n");

	/* ★★★ 新增: 掉线 / 上线检测 */
	{
		uint32_t now = osKernelGetTickCount();
		for (uint8_t i = 1; i < NODE_IDX_MAX; i++)
		{
			if (g_NodeStat[i].rxCount == 0) continue;             /* ★ 从没收到过, 跳过 */

			  if ((now - g_NodeStat[i].lastTick) > 3000U)
			   {
				   g_NodeStat[i].online = 0;
				   Serial_Printf("!! 节点 0x%03X 掉线!\r\n", (unsigned int)(CAN_ID_BASE + i));
			   }

			else
			{
				if (!g_NodeStat[i].online)
				{
					g_NodeStat[i].online = 1;
					Serial_Printf("++ 节点 0x%03X 上线\r\n", (unsigned int)(CAN_ID_BASE + i));
				}
			}
		}
	}

	osDelay(1000);

  }
  /* USER CODE END StartMonitorTask */
}

/* USER CODE BEGIN Header_StartUartRxTask */
/**
* @brief Function implementing the UartRxTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartUartRxTask */
void StartUartRxTask(void *argument)
{
  /* USER CODE BEGIN StartUartRxTask */

	uint8_t b;
	static UartLine_t line;          /* ★ static: 130 字节, 放栈上会爆 */
	uint16_t idx = 0;                /* 当前攒到第几个字节 */
	uint8_t over = 0;
	HAL_UART_Receive_IT(&huart1, &g_uartRxByte, 1);   /* ★ 点火, 全场第一次启动接收 */

	for(;;)
	{
	  if (osMessageQueueGet(UartRxQueueHandle, &b, NULL, 20) == osOK)
	  {
		  g_uartRxCount++;

		  if (b == '\n')                  /* ① 行尾放【最前】判断 */
		  {

              if (over) { g_uartDropLine++; }                  /* ② 超长 → 丢整行 */

              else if (idx > 0)
              {
                  line.len = idx;
                  if (osMessageQueuePut(UartProcQueueHandle, &line, 0, 0) != osOK)
                      g_uartDropLine++;
              }

              idx = 0; over = 0;
           }
		  else if (b != '\r')             /* ② \r 跳过, 其余存 */
		  {
			  if (idx < UART_LINE_MAX) {line.data[idx++] = b;}		//如果还在最大长度内 就保存
			  else over = 1;										//如果超出最大长度 就把超长标志位置1
		  }
	  }
	/* ★ ① 【断帧 B】20ms 没有新字节 → 把攒着的当一包投出去 */
	  else
	  {
		if(over){g_uartDropLine++; over = 0; idx = 0;}
		else if (idx > 0)
		{
		  line.len = idx;
		  if (osMessageQueuePut(UartProcQueueHandle, &line, 0, 0) != osOK)
			  g_uartDropLine++;
		  idx = 0;
		}
	  }

	}

  /* USER CODE END StartUartRxTask */
}

/* USER CODE BEGIN Header_StartUartProcTask */
/**
* @brief Function implementing the UartProcTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartUartProcTask */
void StartUartProcTask(void *argument)
{
  /* USER CODE BEGIN StartUartProcTask */
	UartLine_t line;
	uint8_t n = 0;


	for(;;)
	{
	/* ★ 等一整行（1 秒没来就空转一轮） */
	if (osMessageQueueGet(UartProcQueueHandle, &line, NULL, 1000) == osOK)
	{
		n++;

		if (n >= 100)                      /* ★ 每 100 行汇总报一次 */
		{
		  Serial_Printf("[BATCH] lines=%u bytes=%u drop=%u\r\n",
						(unsigned int)n,
						(unsigned int)g_uartRxCount,     /* 真实收到字节数 */
						(unsigned int)g_uartDropLine);
		  n = 0;
		}
	}
	else if (n > 0)          /* ★ 1 秒没新行 → 这一批发完了, 把余数报出来 */
	{
	  Serial_Printf("[BATCH] lines=%u bytes=%u drop=%u\r\n",
					(unsigned int)n,
					(unsigned int)g_uartRxCount,
					(unsigned int)g_uartDropLine);
	  n = 0;
	}


  }

  /* USER CODE END StartUartProcTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
