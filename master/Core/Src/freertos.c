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


extern CAN_HandleTypeDef hcan1;   /* ★ 引用 can.c 里定义的那个 CAN 句柄 */

extern UART_HandleTypeDef huart1;   /* ★ 引用 usart.c 里定义的串口句柄 */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define NODE_ID   2

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

osMessageQueueId_t CanRxQueueHandle = NULL;     /* ★★★ CAN 接收队列句柄 */

osMessageQueueId_t CanProcQueueHandle = NULL;   /* ★★★ 新增: 处理队列句柄 (CanRxTask → CanProcessTask) */

/* ★★★ ACK 接收状态 —— CanProcessTask 写 / CanTxTask 读 */
volatile uint8_t  g_ackCount  = 0;      /* 累计收到多少个 ACK */
volatile uint8_t  g_ackStatus = 0xFF;   /* 最新 ACK 的结果 (1=成功,0=失败,0xFF=还没收到) */
volatile uint16_t g_ackFrames = 0;      /* 最新 ACK 报的"已收帧数" */

/* ═══════════════════════════════════════════════════════════════
   ★★★ 母机的「固件暂存区」（PC 通过串口下发到这里，再 CAN 转发给子机）

   为什么放 0x08080000:
        · 母机 APP 只占 0x08000000 ~ 0x0807FFFF（512KB，够用）
        · 后半 512KB 空着，划给固件暂存
        必须和 Keil 的 IROM1 Size = 0x00080000 配套改（已完成）

   布局（和子机 B 区同一套思路）:
        +0x0000 ~ +0xFE00    固件数据（最多 65024 字节）
        +0xFF00              魔数（证明固件完整）
        +0xFF04              固件长度（掉电重启后要用）
   ═══════════════════════════════════════════════════════════════ */
#define FW_STORE_ADDR     0x08080000U          /* Sector 8 起点 */
#define FW_STORE_SECTOR   FLASH_SECTOR_8       /* ★ 0x08080000 落在 Sector 8 */
#define FW_STORE_SECTORS  4U                   /* Sector 8~11 = 512KB */
#define FW_MAGIC_ADDR     (FW_STORE_ADDR + 0xFF00U)   /* 魔数 = 0x0808FF00 */
#define FW_LEN_ADDR       (FW_STORE_ADDR + 0xFF04U)   /* 长度 = 0x0808FF04 */
#define FW_MAGIC_VAL      0x5AA5C33CU
#define FW_MAX_LEN        0x0000FE00U          /* 最大 65024 字节（与子机一致） */


/* ═══════════════════════════════════════════════════════════════
   ★★★ PC 下发固件：串口接收状态机

   帧格式（PC → 母机）:
        ┌──────┬──────┬──────────────┬─────────────┐
        │ 0xAA │ 0x55 │  长度 4字节   │  固件数据    │
        │ 帧头1 │ 帧头2 │   (小端)     │  N 字节      │
        └──────┴──────┴──────────────┴─────────────┘
   为什么要帧头: 万一 PC 多按一次发送/数据错位，能重新同步
   为什么先收 RAM 再写 Flash: 子机踩过的坑 —— 边收边写时，
        擦除的 2 秒里数据全丢；所以先收完整再落盘
   ═══════════════════════════════════════════════════════════════ */
#define UART_ST_WAIT_HEAD1   0U     /* 等 0xAA */
#define UART_ST_WAIT_HEAD2   1U     /* 等 0x55 */
#define UART_ST_WAIT_LEN     2U     /* 收 4 字节长度 */
#define UART_ST_WAIT_DATA    3U     /* 收固件数据 */
#define UART_ST_WAIT_CRC     4U     /* ★ 等 4 字节 CRC32 */


static uint8_t  g_fwBufDown[FW_MAX_LEN];   /* ★ 65KB RAM 缓冲（.bss，不占 Flash） */
static uint32_t g_fwDownLen   = 0U;        /* 本包应该收多少字节 */
static uint32_t g_fwDownGot   = 0U;        /* 已经收了多少字节 */
static uint8_t  g_uartState   = UART_ST_WAIT_HEAD1;
static uint8_t  g_lenBuf[4];               /* 暂存 4 字节长度 */
static uint8_t  g_lenIdx = 0U;     /* ★ 当前收到第几个字节（0~3）*/

volatile uint8_t g_uartRxByte  = 0U;       /* ★ 必须是全局/静态：
                                              HAL_UART_Receive_IT 是异步的，
                                              它只记下地址，中断来了才往里写 */
volatile uint8_t g_fwDownReady = 0U;       /* 1 = 收完一包，等 UartRxTask 落盘 */
volatile uint32_t g_fwCrcRecv = 0U;   /* ★ PC 发来的 CRC32（回调写，任务读） */


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
/* Definitions for UartRxTask */
osThreadId_t UartRxTaskHandle;
const osThreadAttr_t UartRxTask_attributes = {
  .name = "UartRxTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for FlashMutex */
osMutexId_t FlashMutexHandle;
const osMutexAttr_t FlashMutex_attributes = {
  .name = "FlashMutex"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* ★★★ 等 ACK: 在超时内等到"新的 ACK"就返回 1, 超时返回 0
        · lastCount = 发帧之前先记下的 g_ackCount
        · 每 10ms 查一次; 用 osDelay(10) 让出 CPU —— 不能忙等,
          否则 CanProcessTask 永远排不上, g_ackCount 也就永远不变 */
static uint8_t WaitAck(uint8_t lastCount, uint32_t timeoutMs)
{
    uint32_t t0 = HAL_GetTick();

    while ((HAL_GetTick() - t0) < timeoutMs)
    {
        if (g_ackCount != lastCount)      /* ★ 计数变了 = 有新 ACK */
        {
            return RES_OK;    /* ★ 等到 = 成功 (全项目统一 0=成功) */
        }

        osDelay(10);                      /* ★ 让出 CPU, 别忙等 */
    }

    return RES_FAIL;                      /* ★ 超时 = 失败 */
}

/* ★ 原型声明：实现写在文件末尾（USER CODE Application 区），
      但 StartUartRxTask 在前面要调用它 → 必须先声明 */
static uint32_t Flash_SaveFirmware(uint32_t len);
/* ★ CRC32（标准 IEEE 802.3）—— 用于校验 PC 下发的固件 */
static uint32_t Crc32_Calc(const uint8_t *p, uint32_t len);

/* USER CODE END FunctionPrototypes */

void StartCanRxTask(void *argument);
void StartCanProcessTask(void *argument);
void StartCanTxTask(void *argument);
void StartUartRxTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */
  /* Create the mutex(es) */
  /* creation of FlashMutex */
  FlashMutexHandle = osMutexNew(&FlashMutex_attributes);

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

  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of CanRxTask */
  CanRxTaskHandle = osThreadNew(StartCanRxTask, NULL, &CanRxTask_attributes);

  /* creation of CanProcessTask */
  CanProcessTaskHandle = osThreadNew(StartCanProcessTask, NULL, &CanProcessTask_attributes);

  /* creation of CanTxTask */
  CanTxTaskHandle = osThreadNew(StartCanTxTask, NULL, &CanTxTask_attributes);

  /* creation of UartRxTask */
  UartRxTaskHandle = osThreadNew(StartUartRxTask, NULL, &UartRxTask_attributes);

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
                    Serial_Printf("★ [心跳] ID=0x%03X 序号=%d\r\n", frame.id, rxSeq);
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


                case FUNC_UP_ACK:                               /* ★★★ 子机回的 ACK */
                {
                    uint8_t  who    = (uint8_t)(frame.id & 0xFFU);   /* 0x201 → 节点 1 */
                    uint8_t  status = frame.data[1];                 /* ★ 结果: 1=成功, 0=失败 */
                    uint16_t frames = (uint16_t)(frame.data[2] | (frame.data[3] << 8));

                    g_ackStatus = status;          /* ★ 新增 */
                    g_ackFrames = frames;          /* ★ 新增 */
                    g_ackCount++;                  /* ★ 新增: 【通知 TxTask 有新的了】*/

                    Serial_Printf("★ [UP-ACK] 节点%u 结果=%s 已收%u帧\r\n",
                                  (unsigned)who,
                                  (status == RES_OK) ? "成功" : "失败",
                                  (unsigned)frames);
                    break;
                }

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
    uint32_t i = 0;
    uint16_t seq = 0;

#define UP_SEND_ID    0x101U     /* ★★★ 【发给子机】—— 子机的号是 0x101 */

    /* 帧头填一次就好（每帧都一样，不用重复填） */
    TxHeader.StdId = UP_ID_DATA_BASE + UP_TARGET_ID;
    TxHeader.ExtId = 0;
    TxHeader.IDE   = CAN_ID_STD;
    TxHeader.RTR   = CAN_RTR_DATA;
    TxHeader.DLC   = 8;
    TxHeader.TransmitGlobalTime = DISABLE;


    for(;;)
    {
        if(HAL_GPIO_ReadPin(GPIOF, GPIO_PIN_2) == 1)
        {
            osDelay(100);
        }
        else
        {
            uint8_t  c;
            uint32_t fwLen;                        /* ★ 运行时从 Flash 读到的长度 */
            uint32_t magic;                        /* ★ 魔数 */
            const uint8_t *fw;                     /* ★ 指向 Flash 暂存区 */
			uint32_t txFail = 0U;                  /* ★ DATA 帧发送失败的次数 */


			/* ★★★ 加锁：独占 Flash 暂存区
               为什么【在检查之前】就加锁:
                 · 魔数和长度都在 Flash 里
                 · 如果先检查再锁，"检查通过"和"开始发送"之间
                   可能被 UartRxTask 插进来擦除 → 读到垃圾 ??
               为什么超时 3000ms 而不是 osWaitForever:
                 · 正常情况下 UartRxTask 擦写约 1 秒
                 · 3 秒拿不到 = 一定有问题，提示用户比死等好 */
            if (osMutexAcquire(FlashMutexHandle, 3000U) != osOK)
            {
                Serial_Printf("[TX] Flash 正忙(可能正在接收新固件), 1秒后重试\r\n");
                osDelay(1000);
                continue;                  /* ★ 没拿到锁 → 不需要释放 */
            }

            /* ═══ ? 先检查 Flash 里到底有没有固件 ═══
               为什么必须先查:
                 没有这个检查，按 Key1 就会发一份【全 0xFF 的垃圾】给子机，
                 子机校验失败、停在 BL —— 用户会以为"升级失败"，
                 其实是"母机根本没固件" */
            magic = *(volatile uint32_t *)FW_MAGIC_ADDR;

            if (magic != FW_MAGIC_VAL)
            {
                Serial_Printf("[TX] Flash 里没有固件(魔数=%08X), 请先用 PC 下发\r\n",
                              (unsigned int)magic);
				osMutexRelease(FlashMutexHandle);      /* ★★★ 提前返回必须释放 */
                osDelay(1000);
                continue;                          /* 回 for 开头等按键 */
            }

            fwLen = *(volatile uint32_t *)FW_LEN_ADDR;

            if (fwLen < 1024U || fwLen > FW_MAX_LEN)
            {
                Serial_Printf("[TX] 固件长度异常 %u, 拒绝发送\r\n", (unsigned int)fwLen);
				osMutexRelease(FlashMutexHandle);      /* ★★★ 提前返回必须释放 */
                osDelay(1000);
                continue;
            }

            Serial_Printf("[TX] 从 Flash 读固件: %u 字节\r\n", (unsigned int)fwLen);

            fw = (const uint8_t *)FW_STORE_ADDR;   /* ★ Flash 当数组用 */

            /* ═══ ① START: 报总长度（失败自动重试 3 次）═══
               为什么要重试:
                 · 按 Key1 时子机可能还没进 BL（10 秒窗口内 / 刚上电）
                 · 只试一次的话，用户看到"无应答"就以为坏了
                 · 3 次 × (2s 等待 + 0.5s 间隔) ≈ 7.5 秒，够子机进 BL
               为什么每次都要重新取 c = g_ackCount:
                 · 重试之间可能收到上一次的迟到 ACK
                 · 不重新记账的话，WaitAck 可能立刻误判"收到"*/
            {
                uint8_t startTries = 0U;
                uint8_t started    = 0U;

                /* 帧内容只填一次（数据不变，不用每次重算） */
                TxData[0] = FUNC_UP_START;
                TxData[1] = (uint8_t)(fwLen & 0xFF);
                TxData[2] = (uint8_t)((fwLen >> 8) & 0xFF);
                TxData[3] = (uint8_t)((fwLen >> 16) & 0xFF);
                TxData[4] = (uint8_t)((fwLen >> 24) & 0xFF);
                TxData[5] = 0;
                TxData[6] = 0;
                TxData[7] = CAN_CalcCRC8(TxData, 7);

                while (startTries < 3U)
                {
                    startTries++;

					/* ★★★ Bus-Off 检测与恢复
                       为什么需要:
                         · CAN 配了 AutoBusOff=ENABLE，但硬件要求
                           "检测到 128×11 个连续隐性位"才自动退出
                         · 总线上只有自己（子机没上电/没进 BL）→ 永远检测不到
                           → 永久卡在 Bus-Off，只能重启板子 ??
                       为什么 Stop+Start 能恢复:
                         · HAL_CAN_Stop 置 MCR.INRQ=1 → 进入【初始化模式】
                         · 初始化模式会强制清掉 Bus-Off 状态
                         · HAL_CAN_Start 再置 INRQ=0 → 回到正常模式 */
                    if ((hcan1.Instance->ESR & CAN_ESR_BOFF) != 0U)
                    {
                        Serial_Printf("[TX] ★ CAN 处于 Bus-Off(ESR=%08X), 主动重启...\r\n",
                                      (unsigned int)hcan1.Instance->ESR);

                        HAL_CAN_Stop(&hcan1);
                        osDelay(20);
                        HAL_CAN_Start(&hcan1);
                        osDelay(20);

                        Serial_Printf("[TX] CAN 已重启, ESR=%08X TEC=%u\r\n",
                                      (unsigned int)hcan1.Instance->ESR,
                                      (unsigned int)((hcan1.Instance->ESR >> 16) & 0xFFU));
                    }

                    c = g_ackCount;        /* ★ 每次重试都重新记账 */

                    if (HAL_CAN_AddTxMessage(&hcan1, &TxHeader, TxData, &TxMailbox) != HAL_OK)
                    {
                        Serial_Printf("! START 发送失败 (第%u次) ErrorCode=%08X TSR=%08X\r\n",
                                      (unsigned int)startTries,
                                      (unsigned int)hcan1.ErrorCode,
                                      (unsigned int)hcan1.Instance->TSR);
                    }

                    osDelay(5);

                    if (WaitAck(c, 2000U) == RES_OK)
                    {
                        started = 1U;
                        break;             /* ★ 收到 ACK，跳出重试 */
                    }

                    Serial_Printf("! START 第 %u 次无应答\r\n", (unsigned int)startTries);
                    osDelay(500);          /* ★ 给子机一点时间进 BL */
                }

                if (started == 0U)
                {
                    Serial_Printf("! START 连续 3 次无应答 请确认: 子机是否已按 Key1 进入 BL\r\n");
                    osMutexRelease(FlashMutexHandle);   /* ★★★ 必须释放 */
                    osDelay(1000);
                    continue;              /* 回 for 开头等按键 */
                }
            }

            /* ═══ ② DATA × N: 每帧 4 字节 ═══ */
            seq = 0;

            for (i = 0; i < fwLen; i += 4U)                   /* ★ 改成 fwLen */
            {
                TxData[0] = FUNC_UP_DATA;
                TxData[1] = (uint8_t)(seq & 0xFF);
                TxData[2] = (uint8_t)(seq >> 8);
                TxData[3] = fw[i + 0U];                       /* ★ 从 Flash 读 */
                TxData[4] = fw[i + 1U];
                TxData[5] = fw[i + 2U];
                TxData[6] = fw[i + 3U];
                TxData[7] = CAN_CalcCRC8(TxData, 7);

				/* ★ 失败只计数，不逐帧打印 —— 7098 帧刷屏会拖垮串口
                   失败通常是"3 个发送邮箱都满"，等一下自然会恢复 */
                if (HAL_CAN_AddTxMessage(&hcan1, &TxHeader, TxData, &TxMailbox) != HAL_OK)
                {
                    txFail++;
                }

                seq++;
                osDelay(1);

            }
			            /* ═══ ③ END: 报总帧数 ═══ */
            c = g_ackCount;                        /* ★ 先记账 */

            TxData[0] = FUNC_UP_END;
            TxData[1] = (uint8_t)(seq & 0xFF);     /* 总帧数, 小端 */
            TxData[2] = (uint8_t)(seq >> 8);
            TxData[3] = 0;
            TxData[4] = 0;
            TxData[5] = 0;
            TxData[6] = 0;
            TxData[7] = CAN_CalcCRC8(TxData, 7);
			/* ★★★ END 是关键帧，值得重试
               为什么:
                 END 丢了 → 子机永远收不到结束信号 →
                 等 10 秒超时 → 整轮 35 秒白跑 ??
               为什么只重试 3 次:
                 每帧 CAN 只要 ~230μs，3 次 + osDelay(2) 才 ~6ms
                 再多就是总线真有问题了 */
            {
                uint8_t tries = 0U;

                while (HAL_CAN_AddTxMessage(&hcan1, &TxHeader, TxData, &TxMailbox) != HAL_OK)
                {
                    tries++;

                    if (tries >= 3U)
                    {
                        Serial_Printf("! END 帧发送失败 %u 次 (ErrorCode=%08X)\r\n",
                                      (unsigned int)tries,
                                      (unsigned int)hcan1.ErrorCode);
                        break;
                    }

                    osDelay(2);      /* ★ 等邮箱空出来再试 */
                }
            }

            /* ★★★ 等子机"写完了"的最终 ACK
               子机收到 END 后会: 擦 B 区(0.5s) → 写(0.7s) → 校验 → 搬运到 A 区
               总共约 2 秒，15 秒超时绰绰有余 */
            if (WaitAck(c, 15000U) == RES_OK)
            {
                Serial_Printf("★★ 子机确认: 结果=%s 已收%u帧 —— 本轮完成!\r\n",
                              (g_ackStatus == RES_OK) ? "成功" : "失败",
                              (unsigned int)g_ackFrames);
            }
            else
            {
                Serial_Printf("! END 无应答\r\n");
            }

			/* ★ 汇总本次发送的丢帧情况 */
            if (txFail > 0U)
            {
                Serial_Printf("! 本轮有 %u 帧未发出(邮箱满), 子机会报丢帧\r\n",
                              (unsigned int)txFail);
            }

			/* ★★★ 发送结束，释放 Flash 使用权
               （UartRxTask 现在可以安全地擦写新固件了） */
            osMutexRelease(FlashMutexHandle);

        }
    }


  /* USER CODE END StartCanTxTask */
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
    /* ★★★ 第一次"上膛" —— 不调这句，RXNEIE 永远是 0，
         数据到了 RXNE 会置位但不产生中断 → 回调永远进不来 */
    if (HAL_UART_Receive_IT(&huart1, (uint8_t *)&g_uartRxByte, 1U) != HAL_OK)
    {
        Serial_Printf("[PC] 串口接收启动失败!\r\n");
    }
    else
    {
        Serial_Printf("[PC] 串口就绪, 等待 PC 下发固件...\r\n");
    }

    for (;;)
    {


        if (g_fwDownReady != 0U)
        {

			/* ★★★ 加锁：独占 Flash 暂存区
               为什么用 osWaitForever（永久等）:
                 · 数据已经安全收在 RAM 的 g_fwBufDown 里了
                 · 等多久都不会丢 —— 就算 CanTxTask 正在发 35 秒，等它发完再写
                 · 用超时反而会"放弃写入"，白白丢掉一次下发 */
            osMutexAcquire(FlashMutexHandle, osWaitForever);


            g_fwDownReady = 0U;      /* ★ 先清标志：处理期间 PC 若再发一包，能重新置 1 */

            Serial_Printf("[PC] 收到固件 %u 字节, 开始保存...\r\n", (unsigned int)g_fwDownLen);

/* ═══ ★★★ CRC32 校验（串口没有硬件校验，这是唯一的一道闸）═══
               为什么放在任务里而不是回调里:
                 算 28392 字节 × 8 次循环 ≈ 5~10ms，
                 放在中断里会阻塞 CAN 等其它中断 → 可能丢帧 ?? */
            {
                uint32_t crcLocal = Crc32_Calc(g_fwBufDown, g_fwDownLen);

                Serial_Printf("[PC] CRC32: 本地=%08X 收到=%08X\r\n",
                              (unsigned int)crcLocal, (unsigned int)g_fwCrcRecv);

                if (crcLocal != g_fwCrcRecv)
                {
                    Serial_Printf("[PC] ? CRC32 不匹配! 数据传输出错, 拒绝写 Flash\r\n");
                }
                else if (Flash_SaveFirmware(g_fwDownLen) == RES_OK)
                {
                    Serial_Printf("[PC] ★ 固件已保存! 按 Key1 可升级子机\r\n");
                }
                else
                {
                    Serial_Printf("[PC] 保存失败, 原有固件不受影响\r\n");
                }
            }

            /* ★★★ 释放：无论成功失败都必须放（原有的一行，保持不变） */
            osMutexRelease(FlashMutexHandle);
        }

        osDelay(50);
	}
  /* USER CODE END StartUartRxTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* ═══════════════════════════════════════════════════════════════
   ★★★ CRC32（标准 IEEE 802.3，多项式 0xEDB88320 反射版）

   为什么要自己写:
        · F4 的硬件 CRC 单元多项式是 0x04C11DB7【不反射】，和标准 CRC32 不一致
        · Python 的 zlib.crc32() 用的正是标准 CRC32
        · 两边必须一致 → C 端手写

   为什么在任务里算而不是在中断里:
        · 28392 字节 × 8 次循环 ≈ 5~10ms
        · 中断里跑 10ms 会阻塞 CAN 等其它中断 → 可能丢帧
   ═══════════════════════════════════════════════════════════════ */
static uint32_t Crc32_Calc(const uint8_t *p, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t i, k;

    for (i = 0U; i < len; i++)
    {
        crc ^= (uint32_t)p[i];

        for (k = 0U; k < 8U; k++)
        {
            if ((crc & 1U) != 0U)
            {
                crc = (crc >> 1) ^ 0xEDB88320U;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return ~crc;            /* ★ 标准 CRC32 的最后一步：结果取反 */
}

/* ═══════════════════════════════════════════════════════════════
   ★★★ 串口接收完成回调（每收 1 字节进一次）
         运行在【中断上下文】里，所以:
            · 不能 osDelay / 不能阻塞
            · 不能调 FreeRTOS 的阻塞型 API
            · 只做"搬字节 + 推进状态机"这种微秒级的事
   ═══════════════════════════════════════════════════════════════ */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    uint8_t b;

    if (huart->Instance != USART1)
    {
        return;                     /* 不是 USART1 的回调，忽略 */
    }

    b = g_uartRxByte;               /* ★ 先把刚收到的字节取走 */

    /* ★★★ 链式武装：必须在处理数据【之前】重新挂上
       原因: HAL 收满 1 字节后会自动 __HAL_UART_DISABLE_IT(RXNEIE)，
             不重新挂 → 第 2 个字节起永远收不到 */
    HAL_UART_Receive_IT(&huart1, (uint8_t *)&g_uartRxByte, 1U);

    switch (g_uartState)
    {
        /* ─── 等帧头 1: 0xAA ─── */
        case UART_ST_WAIT_HEAD1:
            if (b == 0xAAU)
            {
                g_uartState = UART_ST_WAIT_HEAD2;
            }

            break;

        /* ─── 等帧头 2: 0x55 ─── */
        case UART_ST_WAIT_HEAD2:
            if (b == 0x55U)
            {
                g_uartState = UART_ST_WAIT_LEN;
                g_lenIdx    = 0U;
            }
            else if (b == 0xAAU)
            {
                /* ★ 连续两个 0xAA：当作新的帧头 1，不丢弃 */
                g_uartState = UART_ST_WAIT_HEAD2;
            }
            else
            {
                g_uartState = UART_ST_WAIT_HEAD1;   /* 帧头不对 → 重新找 */
            }

            break;

        /* ─── 收 4 字节长度（小端） ─── */
        case UART_ST_WAIT_LEN:
            g_lenBuf[g_lenIdx] = b;
            g_lenIdx++;

            if (g_lenIdx >= 4U)
            {
                g_lenIdx = 0U;

                g_fwDownLen = (uint32_t)g_lenBuf[0]
                              | ((uint32_t)g_lenBuf[1] << 8)
                              | ((uint32_t)g_lenBuf[2] << 16)
                              | ((uint32_t)g_lenBuf[3] << 24);
                g_fwDownGot = 0U;

                /* ★ 长度合理性检查：太短不是固件，太长装不下 */
                if (g_fwDownLen < 1024U || g_fwDownLen > FW_MAX_LEN)
                {
                    g_uartState = UART_ST_WAIT_HEAD1;   /* 非法 → 丢弃，重找帧头 */
                }
                else
                {
                    g_uartState = UART_ST_WAIT_DATA;
                }
            }

            break;

        /* ─── 收固件数据 ─── */
        case UART_ST_WAIT_DATA:
            if (g_fwDownGot < FW_MAX_LEN)
            {
                g_fwBufDown[g_fwDownGot] = b;
                g_fwDownGot++;
            }

            if (g_fwDownGot >= g_fwDownLen)
            {
                /* ★★★ 数据收完了，但【先别交给任务】—— 后面还有 4 字节 CRC32 */
                g_lenIdx      = 0U;                     /* 复用 g_lenBuf 暂存 */
                g_uartState   = UART_ST_WAIT_CRC;
            }

            break;

			/* ★★★ 收 4 字节 CRC32（小端）—— 只存不解算，算的活交给任务 */
			case UART_ST_WAIT_CRC:
			g_lenBuf[g_lenIdx] = b;
			g_lenIdx++;

			if (g_lenIdx >= 4U)
			{
				g_fwCrcRecv = (uint32_t)g_lenBuf[0]
						| ((uint32_t)g_lenBuf[1] << 8)
						| ((uint32_t)g_lenBuf[2] << 16)
						| ((uint32_t)g_lenBuf[3] << 24);

				g_lenIdx      = 0U;
				g_fwDownReady = 1U;      /* ★ 整包（数据+CRC）收齐了，交给任务 */
				g_uartState   = UART_ST_WAIT_HEAD1;
			}
			break;


        default:
            g_uartState = UART_ST_WAIT_HEAD1;
            break;
    }
}

/* ═══════════════════════════════════════════════════════════════
   ★★★ 把 RAM 里收到的固件写进 Flash 暂存区（0x08080000）

   顺序（和子机、双区同一个原则）:
        擦 → 写 → 校验 → ★【全部通过后才写魔数+长度】
   为什么魔数最后写:
        提前写的话，写到一半失败会出现"魔数有、数据残缺"的假有效，
        下次按 Key1 会发一份坏固件给子机。
   ═══════════════════════════════════════════════════════════════ */
static uint32_t Flash_SaveFirmware(uint32_t len)
{
    uint32_t i, k;
    uint32_t bad     = 0xFFFFFFFFU;
    uint32_t nSectors;
    uint32_t word;
    uint32_t expect;
    uint32_t *pRd;
    FLASH_EraseInitTypeDef er;
    uint32_t secErr  = 0U;

    /* ─── ① 长度检查 ─── */
    if (len < 1024U || len > FW_MAX_LEN)
    {
        Serial_Printf("[PC] 长度异常 %u, 拒绝保存\r\n", (unsigned int)len);
        return RES_FAIL;
    }

    /* ─── ② 算要擦几个扇区（Sector 8~11 都是 128KB，可直接除）─── */
    nSectors = (len + (128U * 1024U) - 1U) / (128U * 1024U);

    if (nSectors == 0U)
    {
        nSectors = 1U;
    }

    if (nSectors > FW_STORE_SECTORS)
    {
        nSectors = FW_STORE_SECTORS;
    }

    /* ═══ ③ 擦除 ═══ */
    Serial_Printf("[PC] 擦除 Sector 8 起 %u 个扇区...\r\n", (unsigned int)nSectors);
    HAL_FLASH_Unlock();

    er.TypeErase    = FLASH_TYPEERASE_SECTORS;
    er.Banks        = FLASH_BANK_1;
    er.Sector       = FW_STORE_SECTOR;
    er.NbSectors    = nSectors;
    er.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    if (HAL_FLASHEx_Erase(&er, &secErr) != HAL_OK)
    {
        HAL_FLASH_Lock();
        Serial_Printf("[PC] 擦除失败 secErr=%08X\r\n", (unsigned int)secErr);
        return RES_FAIL;
    }

    /* ═══ ④ 写入（RAM 缓冲 → Flash，逐 WORD）═══ */
    Serial_Printf("[PC] 写入中...\r\n");

    for (i = 0U; i + 4U <= len; i += 4U)
    {
        word = (uint32_t)g_fwBufDown[i]
               | ((uint32_t)g_fwBufDown[i + 1U] << 8)
               | ((uint32_t)g_fwBufDown[i + 2U] << 16)
               | ((uint32_t)g_fwBufDown[i + 3U] << 24);

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FW_STORE_ADDR + i, word) != HAL_OK)
        {
            HAL_FLASH_Lock();
            Serial_Printf("[PC] 写入失败 @%u\r\n", (unsigned int)i);
            return RES_FAIL;
        }
    }

    HAL_FLASH_Lock();

    /* ═══ ⑤ 读回校验 ═══ */
    pRd = (uint32_t *)FW_STORE_ADDR;

    for (k = 0U; k + 4U <= len; k += 4U)
    {
        expect = (uint32_t)g_fwBufDown[k]
                 | ((uint32_t)g_fwBufDown[k + 1U] << 8)
                 | ((uint32_t)g_fwBufDown[k + 2U] << 16)
                 | ((uint32_t)g_fwBufDown[k + 3U] << 24);

        if (pRd[k / 4U] != expect)
        {
            bad = k;
            break;
        }
    }

    if (bad != 0xFFFFFFFFU)
    {
        /* ★ 校验失败 → 魔数/长度都不写 → 下次按 Key1 会判"无固件" */
        Serial_Printf("[PC] 校验失败 @%u (魔数未写, 不会发给子机)\r\n", (unsigned int)bad);
        return RES_FAIL;
    }

    /* ═══ ⑥ 校验通过 → 才写魔数 + 长度 ═══ */
    HAL_FLASH_Unlock();

    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FW_MAGIC_ADDR, FW_MAGIC_VAL) != HAL_OK)
    {
        HAL_FLASH_Lock();
        Serial_Printf("[PC] 写魔数失败\r\n");
        return RES_FAIL;
    }

    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FW_LEN_ADDR, len) != HAL_OK)
    {
        HAL_FLASH_Lock();
        Serial_Printf("[PC] 写长度失败\r\n");
        return RES_FAIL;
    }

    HAL_FLASH_Lock();
    return RES_OK;
}


//D:\18_Python\python.exe "D:\CubeMX\RTOS\BL\_agent-notes\send_fw.py" COM10


/* USER CODE END Application */
