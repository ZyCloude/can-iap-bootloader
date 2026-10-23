/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "cmsis_os.h"
#include "can.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "cmsis_os.h"              /* ★ CMSIS-RTOS2 的队列 API */
#include <string.h>                /* ★ memcpy */
#include "MYUSART.h"    		   /* ★ 提供 Serial_Printf —— ★ 函数名按你 MYUSART.h 里实际的来 */

extern osMessageQueueId_t CanRxQueueHandle;   /* ★ 队列句柄(在 freertos.c 里定义) */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#define APP_ADDR   0x08010000U          /* 		 必须和 APP 的 IROM1 Start 一致 */
#define APP_SECTOR_FIRST  FLASH_SECTOR_4        /* APP 区起始扇区 */
#define APP_SECTOR_LAST   FLASH_SECTOR_11       /* APP 区最后扇区 */
#define RX_TIMEOUT_MS     200U                  /* 200ms 没新字节 → 认为传完 */

/* ★★★ 断电保护魔数
   位置: Sector 4 内、距 APP_ADDR 0xFF00 (65280) 处
   为什么放这: 擦除优化后只擦"需要的扇区"(当前只擦 Sector 4)，
              魔数必须落在【会被擦到】的地址，否则旧魔数残留 → 误判有效
   限制: 固件最大 0xFE00 (65024) 字节，给魔数留 256 字节余量
  若以后固件涨到 64KB 以上(要擦 2 个扇区)，这个地址要重新设计 */
#define APP_MAGIC_ADDR   (APP_ADDR + 0xFF00U)
#define APP_MAGIC_VAL    0x5AA5C33CU
#define APP_FW_MAX_LEN   0xFE00U

/* ═══════════════════════════════════════════════════════════════
   ★★★ 双区回滚分区（路线2：新固件先写 B 区 → 校验 → 搬运到 A 区）

   为什么用"搬运"而不是"两份固件"：
        A 区在 0x08010000、B 区在 0x08080000，地址不同，
        同一份 bin 不能两边通用（链接器把绝对地址写死了）。
        所以：只生成一份固件（按 A 区地址链接）→ 先写 B 区校验 →
              校验通过后再搬回 A 区。
   ═══════════════════════════════════════════════════════════════ */
#define APP_A_ADDR        0x08010000U   /* A 区（真正运行） */
#define APP_A_SECTOR      FLASH_SECTOR_4
#define APP_A_SECTORS     4U            /* Sector 4~7 = 448KB */

#define APP_B_ADDR        0x08080000U   /* B 区（升级暂存） */
#define APP_B_SECTOR      FLASH_SECTOR_8
#define APP_B_SECTORS     3U            /* Sector 8~10 = 384KB */

/* 标志区：日志式多槽，128 个槽 × 1KB = 128KB（Sector 11） */
#define FLAG_AREA_ADDR    0x080E0000U
#define FLAG_AREA_SECTOR  FLASH_SECTOR_11
#define FLAG_SLOT_SIZE    0x400U
#define FLAG_SLOT_COUNT   (128U * 1024U / FLAG_SLOT_SIZE)   /* = 128 */

/* 状态值（高 16 位是防误判的暗号） */
#define SYS_STATE_MAGIC      0xA5A50000U
#define SYS_STATE_NORMAL     0x00000000U   /* 正常：跳 A 区 */
#define SYS_STATE_COPYING    0x00000001U   /* 待搬运：B → A */

typedef void (*pFunction)(void);

static void JumpToApp(uint32_t appAddr)
{
    pFunction jump;
    uint32_t  sp, pc;
	uint32_t  i;            /* ★ 新增: 清 NVIC 用的循环变量 */

    sp = *(volatile uint32_t *)appAddr;              /* 取初始栈顶 */
    pc = *(volatile uint32_t *)(appAddr + 4U);       /* 取 Reset_Handler */

    __disable_irq();

//    HAL_RCC_DeInit();
//    HAL_DeInit();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;

	for (i = 0U; i < 8U; i++)                   /* ③ 所有 NVIC 中断: 全关 + 全清 */
    {
        NVIC->ICER[i] = 0xFFFFFFFFU;            /*   关使能 */
        NVIC->ICPR[i] = 0xFFFFFFFFU;            /*   清挂起 */
    }
    Serial_Printf("[BL]正在跳转到 APP...\r\n");
    SCB->VTOR = appAddr;
    __set_MSP(sp);
    jump = (pFunction)pc;
    __enable_irq();
    jump();
}

/* ★★★ 检查 APP 区里是不是一份有效固件
       判据: 向量表第一个字 = 初始栈顶(SP), 必须落在 SRAM 范围内
             F407 的 SRAM = 0x20000000 ~ 0x20020000 (128KB)
             擦干净的 Flash 是 0xFFFFFFFF → 不在范围内 → 判为无效
       返回: RES_OK(0) = 有效,  RES_FAIL(1) = 无效
       ★ 全项目统一: 0=成功/有效, 1=失败/无效 */
static uint32_t CheckApp(uint32_t appAddr)
{
    uint32_t sp    = *(volatile uint32_t *)appAddr;
    /* ★ 魔数按"传入的区"算偏移 —— A 区得到 0x0801FF00，B 区得到 0x0808FF00 */
    uint32_t magic = *(volatile uint32_t *)(appAddr + 0xFF00U);

    /* ① 栈顶必须落在 SRAM 范围 */
    if (sp < 0x20000000U || sp > 0x20020000U)
    {
        return RES_FAIL;      /* 无效: 空的 / 被擦过 */
    }

    /* ② 魔数必须对 —— 防"写到一半断电"的残骸被误判为有效 */
    if (magic != APP_MAGIC_VAL)
    {
        return RES_FAIL;      /* 无效: 数据可能不完整 */
    }

    return RES_OK;            /* 有效 */
}

/* ★★★ 回一帧 ACK 给母机
       status : 0=OK, 1=错误
       frames : 已收帧数
       ★ ID = 0x200 + 自己的节点号 → 母机【一眼看出是哪台回的】     */
static void Can_SendAck(uint8_t status, uint16_t frames)
{
    CAN_TxHeaderTypeDef h;
    uint8_t  d[8];
    uint32_t mailbox;

    d[0] = FUNC_UP_ACK;
    d[1] = status;
    d[2] = (uint8_t)(frames & 0xFF);        /* 已收帧数, 小端 */
    d[3] = (uint8_t)(frames >> 8);
    d[4] = 0;
    d[5] = 0;
    d[6] = 0;
    d[7] = CAN_CalcCRC8(d, 7);              /* ★ Data[7] = CRC8 */

    h.StdId = UP_ID_ACK_BASE + MY_NODE_ID;  /* ★★ = 0x201 */
    h.ExtId = 0;
    h.IDE   = CAN_ID_STD;
    h.RTR   = CAN_RTR_DATA;
    h.DLC   = 8;
    h.TransmitGlobalTime = DISABLE;

    HAL_CAN_AddTxMessage(&hcan1, &h, d, &mailbox);
}

/* ★★★ 算 A 区要擦几个扇区
       A 区从 Sector 4 开始，而 F4 的扇区大小不统一:
           Sector 4    = 64KB
           Sector 5~11 = 128KB
       所以不能直接除，必须【一个扇区一个扇区地累加】 */
static uint32_t CalcEraseSectors(uint32_t len)
{
    uint32_t off     = 0U;                 /* 已经"覆盖"了多少字节 */
    uint32_t sec     = APP_SECTOR_FIRST;   /* 当前看到第几个扇区 */
    uint32_t cnt     = 0U;                 /* 已经数了几个扇区 */
    uint32_t secSize;                      /* 当前这个扇区有多大 */

    while (off < len && sec <= APP_SECTOR_LAST)
    {
        /* 每个扇区单独判断大小 */
        if (sec == FLASH_SECTOR_4)
        {
            secSize = 64U * 1024U;         /* Sector 4 = 64KB */
        }
        else
        {
            secSize = 128U * 1024U;        /* Sector 5~11 = 128KB */
        }

        off = off + secSize;               /* 累计覆盖的字节数 */
        cnt = cnt + 1U;                    /* 又用掉一个扇区 */
        sec = sec + 1U;                    /* 接着看下一个扇区 */
    }

    if (cnt == 0U)
    {
        cnt = 1U;                          /* 保险: 至少擦 1 个 */
    }

    return cnt;
}

/* ★★★ 算 B 区要擦几个扇区
       B 区从 Sector 8 开始，Sector 8/9/10 【全是 128KB】 → 大小统一
       所以【不需要】像 A 区那样逐扇区判断大小，直接累加就行 */
static uint32_t CalcSectorsForB(uint32_t len)
{
    uint32_t off     = 0U;
    uint32_t cnt     = 0U;
    uint32_t secSize = 128U * 1024U;       /* B 区每个扇区都是 128KB */
    uint32_t maxSec  = APP_B_SECTORS;      /* B 区总共只有 3 个扇区 */

    while (off < len && cnt < maxSec)
    {
        off = off + secSize;
        cnt = cnt + 1U;
    }

    if (cnt == 0U)
    {
        cnt = 1U;                          /* 保险: 至少擦 1 个 */
    }

    return cnt;
}

/* ═══════════════════════════════════════════════════════════════
   ★★★ 把 B 区的固件搬到 A 区（双区回滚的核心）

   调用时机有两个:
      · 升级流程里: B 区写完校验通过 → 立刻搬
      · 开机时:     标志区显示"待搬运"(说明上次搬一半断电了) → 再搬一次

   为什么"搬运"是幂等的(可以重复执行):
      · 每次都是"擦 A 区 → 从 B 区完整复制一份 → 校验"
      · 所以断电重来多少次，结果都一样 ?
   ═══════════════════════════════════════════════════════════════ */
static uint32_t CopyBtoA(uint32_t len)
{
    uint32_t i;
    uint32_t word;
    FLASH_EraseInitTypeDef er;
    uint32_t secErr = 0U;

	 /* ★ 如果传 0，就从 B 区读存好的长度（断电重启后走这条路） */
    if (len == 0U)
    {
        len = *(volatile uint32_t *)(APP_B_ADDR + 0xFF04U);
        Serial_Printf("[BL]搬运: 从 B 区读到长度 %u\r\n", (unsigned int)len);
    }

    if (len < 1024U || len > APP_FW_MAX_LEN)
    {
        return RES_FAIL;
    }

    /* ═══ ① 擦 A 区（A 区用逐扇区累加的算法，因为 Sector4 只有 64KB） ═══ */
    Serial_Printf("[BL]搬运: 擦除 A 区...\r\n");
    HAL_FLASH_Unlock();

    er.TypeErase    = FLASH_TYPEERASE_SECTORS;
    er.Banks        = FLASH_BANK_1;
    er.Sector       = APP_A_SECTOR;
    er.NbSectors    = CalcEraseSectors(len);      /* ← 原来的 A 区算法 */
    er.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    if (HAL_FLASHEx_Erase(&er, &secErr) != HAL_OK)
    {
        Serial_Printf("[BL]搬运: 擦 A 区失败 secErr=%08X\r\n", (unsigned int)secErr);
        HAL_FLASH_Lock();
        return RES_FAIL;
    }

//	/* ═══════════════════════════════════════════════════════════
//       ★★★ 临时测试代码（测完必须删掉！）
//       目的: 制造 5 秒窗口，好在"A 区已擦空、B 区完整"的
//             最坏时刻拔电，验证重启后能否自愈
//       ?? 延时期间先 Lock，避免长时间持着 Flash 解锁状态
//       ═══════════════════════════════════════════════════════════ */
//    Serial_Printf("[BL]★★ A 区已擦空! 5 秒后开始复制 —— 现在拔电可测自愈\r\n");
//    HAL_FLASH_Lock();
//    HAL_Delay(5000);
//    HAL_FLASH_Unlock();
//    /* ═══════════ 临时测试代码结束 ═══════════ */

    /* ═══ ② B 区 → A 区 逐字复制 ═══ */
    Serial_Printf("[BL]搬运: 复制 %u 字节...\r\n", (unsigned int)len);

    for (i = 0U; i + 4U <= len; i += 4U)
    {
        word = *(volatile uint32_t *)(APP_B_ADDR + i);   /* 从 B 读 */

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_A_ADDR + i, word) != HAL_OK)
        {
            Serial_Printf("[BL]搬运: 写入失败 @%u\r\n", (unsigned int)i);
            HAL_FLASH_Lock();
            return RES_FAIL;
        }
    }

    /* ═══ ③ 先把 A 区解锁关掉（复制完了） ═══ */
    HAL_FLASH_Lock();

    /* ═══ ④ 校验 A 区（和 B 区逐字比对） ═══ */
    {
        uint32_t *p = (uint32_t *)APP_A_ADDR;
        uint32_t k, bad = 0xFFFFFFFFU;

        for (k = 0U; k + 4U <= len; k += 4U)
        {
            uint32_t src = *(volatile uint32_t *)(APP_B_ADDR + k);

            if (p[k / 4U] != src)
            {
                bad = k;
                break;
            }
        }

        if (bad != 0xFFFFFFFFU)
        {
            Serial_Printf("[BL]搬运: 校验失败 @%u\r\n", (unsigned int)bad);
            return RES_FAIL;      /* ★ 魔数没写 → 下次开机判无效 → 停 BL 可自救 */
        }
    }

    /* ═══ ⑤ ★★★ 校验通过之后才写魔数（断电保护的关键） ═══
       如果提前写，搬运失败时 A 区会有"魔数有、数据残缺"的假有效，
       下次开机跳进残骸 → 变砖 */
    HAL_FLASH_Unlock();
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                          APP_A_ADDR + 0xFF00U, APP_MAGIC_VAL) != HAL_OK)
    {
        Serial_Printf("[BL]搬运: 写 A 区魔数失败\r\n");
        HAL_FLASH_Lock();
        return RES_FAIL;
    }
    HAL_FLASH_Lock();

    Serial_Printf("[BL]搬运完成, A 区已就绪\r\n");
    return RES_OK;

}

/* ═══ 标志区：读当前状态 ═══
   日志式读法：从槽 0 往后扫，遇到第一个 0xFFFFFFFF(空槽) 就停，
               它【前面那个槽】就是最新状态。
   为什么这样读: Flash 只能"擦完重写"，不能原地改；
                 日志式靠"往后追加"，任何时刻断电都不会丢状态 */
static uint32_t Flag_GetState(void)
{
    uint32_t i;
    uint32_t val;

    for (i = 0U; i < FLAG_SLOT_COUNT; i++)
    {
        val = *(volatile uint32_t *)(FLAG_AREA_ADDR + i * FLAG_SLOT_SIZE);

        if (val == 0xFFFFFFFFU)           /* 第一个空槽 */
        {
            if (i == 0U)
            {
                return SYS_STATE_NORMAL;  /* 从没写过 → 默认正常 */
            }
            val = *(volatile uint32_t *)(FLAG_AREA_ADDR + (i - 1U) * FLAG_SLOT_SIZE);
            /* 高 16 位是暗号才算有效，否则当垃圾忽略 */
            return ((val & 0xFFFF0000U) == SYS_STATE_MAGIC)
                   ? (val & 0x0000FFFFU) : SYS_STATE_NORMAL;
        }
    }

    /* 槽全用满了，取最后一个 */
    val = *(volatile uint32_t *)(FLAG_AREA_ADDR + (FLAG_SLOT_COUNT - 1U) * FLAG_SLOT_SIZE);
    return ((val & 0xFFFF0000U) == SYS_STATE_MAGIC)
           ? (val & 0x0000FFFFU) : SYS_STATE_NORMAL;
}

/* ═══ 标志区：写新状态（追加到第一个空槽，不擦除） ═══ */
static uint32_t Flag_SetState(uint32_t state)
{
    uint32_t i;
    uint32_t val;
    FLASH_EraseInitTypeDef er;
    uint32_t secErr = 0U;

    /* 找第一个空槽 */
    for (i = 0U; i < FLAG_SLOT_COUNT; i++)
    {
        val = *(volatile uint32_t *)(FLAG_AREA_ADDR + i * FLAG_SLOT_SIZE);
        if (val == 0xFFFFFFFFU)
        {
            break;
        }
    }

    HAL_FLASH_Unlock();

    /* 槽全用完了（128 次状态变化）→ 擦整个扇区，从槽 0 重来 */
    if (i >= FLAG_SLOT_COUNT)
    {
        er.TypeErase    = FLASH_TYPEERASE_SECTORS;
        er.Banks        = FLASH_BANK_1;
        er.Sector       = FLAG_AREA_SECTOR;
        er.NbSectors    = 1U;
        er.VoltageRange = FLASH_VOLTAGE_RANGE_3;

        if (HAL_FLASHEx_Erase(&er, &secErr) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return RES_FAIL;
        }
        i = 0U;
    }

    /* 追加写一个槽（只写 4 字节，不擦除 → 几微秒） */
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                          FLAG_AREA_ADDR + i * FLAG_SLOT_SIZE,
                          SYS_STATE_MAGIC | (state & 0xFFFFU)) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return RES_FAIL;
    }

    HAL_FLASH_Lock();
    return RES_OK;
}

/* ★ 固件缓冲: 32KB, 放 .bss(不占栈) */
#define APP_MAX_SIZE   0xFE00U
static uint8_t g_fwBuf[APP_MAX_SIZE];
/* ★★★ 通用: 把 g_fwBuf 里的 len 字节, 擦 → 写 → 校验 进 APP 区
       返回 1 = 通过, 0 = 失败
       ★ 串口版和 CAN 版【共用这一个函数】                              */
static uint32_t Flash_WriteFirmware(uint32_t len)
{
    uint32_t i;
    FLASH_EraseInitTypeDef er;
    uint32_t secErr = 0;

    /* ★ 长度合理性检查: 太短 → 根本不是固件, 别擦! */
    if (len < 1024U || len > APP_FW_MAX_LEN)
    {
        Serial_Printf("[BL]长度异常(%u), 拒绝升级, 不擦除\r\n", (unsigned int)len);
        return RES_FAIL;
    }

    /* ═══ ① 擦除 B 区（双区后：新固件先写 B 区，A 区不动）═══ */
    Serial_Printf("[BL]开始擦除 B 区... (固件 %u 字节 → 擦 %u 个扇区)\r\n",
                  (unsigned int)len,
                  (unsigned int)CalcSectorsForB(len));
    HAL_FLASH_Unlock();

    er.TypeErase    = FLASH_TYPEERASE_SECTORS;
    er.Banks        = FLASH_BANK_1;
    er.Sector       = APP_B_SECTOR;              /* ★ Sector 8 */
    er.NbSectors    = CalcSectorsForB(len);      /* ★ B 区全是 128KB 扇区 */
    er.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    if (HAL_FLASHEx_Erase(&er, &secErr) != HAL_OK)
    {
        Serial_Printf("[BL]擦除 B 区失败 secErr=%08X\r\n", (unsigned int)secErr);
        HAL_FLASH_Lock();
        return RES_FAIL;
    }

    /* ═══ ② 写入 B 区 ═══ */
    Serial_Printf("[BL]开始写入 B 区...\r\n");

    for (i = 0; i + 4U <= len; i += 4U)
    {
        uint32_t word = (uint32_t)g_fwBuf[i]
                        | ((uint32_t)g_fwBuf[i + 1] << 8)
                        | ((uint32_t)g_fwBuf[i + 2] << 16)
                        | ((uint32_t)g_fwBuf[i + 3] << 24);

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_B_ADDR + i, word) != HAL_OK)
        {
            Serial_Printf("[BL]写入 B 区失败 @%u\r\n", (unsigned int)i);
            HAL_FLASH_Lock();
            return RES_FAIL;
        }
    }

    HAL_FLASH_Lock();
    Serial_Printf("[BL]B 区写入结束 共写入%u字节\r\n", (unsigned int)i);

    /* ═══ ③ 校验 B 区（和 RAM 里的 g_fwBuf 逐字比对）═══ */
    {
        uint32_t *p = (uint32_t *)APP_B_ADDR;     /* ★ 校验 B 区 */
        uint32_t k, bad = 0xFFFFFFFFU;

        for (k = 0U; k + 4U <= len; k += 4U)
        {
            uint32_t expect = (uint32_t)g_fwBuf[k]
                              | ((uint32_t)g_fwBuf[k + 1] << 8)
                              | ((uint32_t)g_fwBuf[k + 2] << 16)
                              | ((uint32_t)g_fwBuf[k + 3] << 24);

            if (p[k / 4U] != expect)
            {
                bad = k;
                break;
            }
        }

        if (bad != 0xFFFFFFFFU)
        {
            Serial_Printf("[BL]B 区校验失败 @%u\r\n", (unsigned int)bad);
            return RES_FAIL;   /* ★ 魔数/标志都不写 → 下次开机照跑 A 区, 安全 */
        }
    }

    /* ═══ ④ ★★★ 校验通过 → 写 B 区魔数 + 长度 + 标志"待搬运" ═══
       为什么顺序是这样:
         · 先写 B 区魔数和长度 → 证明"B 区是一份完整固件"
         · 再写标志"待搬运"   → 告诉下次开机"该搬了"
       如果魔数写失败就直接返回, 不写标志 →
       下次开机看到标志是"正常" → 照跑 A 区, 不会去搬一份坏固件 ?
       长度为什么存 B 区:
         · 断电重启后 RAM 里的 len 丢了
         · 搬运时需要知道长度, 所以和魔数一起存在 B 区 */
    HAL_FLASH_Unlock();

    /* ④-1 B 区魔数（证明完整） */
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                          APP_B_ADDR + 0xFF00U, APP_MAGIC_VAL) != HAL_OK)
    {
        Serial_Printf("[BL]写 B 区魔数失败\r\n");
        HAL_FLASH_Lock();
        return RES_FAIL;
    }

    /* ④-2 B 区固件长度（搬运时要用） */
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                          APP_B_ADDR + 0xFF04U, len) != HAL_OK)
    {
        Serial_Printf("[BL]写 B 区长度失败\r\n");
        HAL_FLASH_Lock();
        return RES_FAIL;
    }

    HAL_FLASH_Lock();

    /* ④-3 标志区：标记"待搬运" */
    if (Flag_SetState(SYS_STATE_COPYING) != RES_OK)
    {
        Serial_Printf("[BL]写标志失败\r\n");
        return RES_FAIL;
    }

    Serial_Printf("[BL]B 区就绪! (魔数+长度已写, 标志=待搬运)\r\n");
    return RES_OK;
}

static uint32_t DoUpGrade(uint8_t firstByte)
{
    uint32_t len = 0;
    uint8_t  tmp;

    /* ═══ ① 先收完(存进 RAM) ═══ */
    g_fwBuf[len++] = firstByte;              /* 探针那字节 */

    for (;;)
    {
        if (len >= APP_MAX_SIZE)
        {
            break;    /* 装满保护 */
        }

        if (HAL_UART_Receive(&huart1, &tmp, 1, RX_TIMEOUT_MS) != HAL_OK)
        {
            break;    /* ★ 1000ms 没新字节 → 收完了 */
        }

        g_fwBuf[len++] = tmp;
    }

    Serial_Printf("[BL]收到 %u 字节\r\n", (unsigned int)len);

    /* ═══ ②③④ 全部交给通用函数 ═══ */
    return Flash_WriteFirmware(len);
}


/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */


  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_CAN1_Init();
  MX_USART1_UART_Init();
  HAL_Delay(10);        /* ★ 等 TX 引脚电平稳定, 再开始打印 */
  /* USER CODE BEGIN 2 */

    /* ★★★ 上电判断: 给 10 秒窗口
        · 这 10 秒内【按一下 Key1】     → 进升级模式(停在 BL)
        · 10 秒内没按 且 APP 有效       → 直接跳过去跑 APP
        · APP 无效(没升级过/被擦坏)     → 停在 BL 等升级
       为什么用"10 秒窗口"而不是"上电前按住":
         从上电到首次读引脚只有几毫秒，人手根本来不及按*/
    {
        uint32_t t0;
        uint8_t  keyPressed = 0U;        /* 声明放块首: ARMCC 默认 C90 */

        Serial_Printf("[BL]10 秒内按 Key1 进入升级模式...\r\n");
        t0 = HAL_GetTick();

        while ((HAL_GetTick() - t0) < 10000U)
        {
            if (HAL_GPIO_ReadPin(Key1_GPIO_Port, Key1_Pin) == GPIO_PIN_RESET)
            {
                keyPressed = 1U;
                break;                   /* ★ 一按到就立刻停等, 不用等满 10 秒 */
            }
            HAL_Delay(10);               /* ★ BL 里 RTOS 没启动 → 必须用 HAL_Delay */
        }

		if (keyPressed != 0U)
        {
            Serial_Printf("[BL]检测到 Key1 → 进入升级模式\r\n");
        }
        else
        {
            /* ★ C90: 变量声明必须放块首 */
            uint32_t st;

            /* ★★★ 第一步: 先看标志区有没有"待搬运"
               (上次升级完 B 区, 但还没搬到 A 区就被断电了)
               ★ 这个检查必须放在 CheckApp 之前 ——
                 因为 A 区此刻可能是残骸, CheckApp 会判无效,
                 但 B 区其实有一份完整固件可以搬 ? */
            st = Flag_GetState();

			if (st == SYS_STATE_COPYING)
            {
                Serial_Printf("[BL]发现待搬运标志 → 检查 B 区...\r\n");

				//因为本质这是第二次擦写了 copybtoa只能检测ab区的数值是不是一样的 但是无法判断b区的数值是否正确
				//所以要提前检测一下b区的正确性 在正确的情况下再擦写

                /* ★★★ 关键: 搬运前先确认 B 区真的是一份完整固件
                   (栈顶合法 + 魔数正确)
                   为什么必须查:
                     CopyBtoA 的校内验只能证明"复制没出错",
                     不能证明"B 区的源数据是对的" ——
                     如果 B 区是坏的, 复制+校验会"通过", A 区被写成垃圾 ? */
                if (CheckApp(APP_B_ADDR) != RES_OK)
                {
                    Serial_Printf("[BL]B 区无效! 标志作废, 留在 BL 等重新升级\r\n");
                    Flag_SetState(SYS_STATE_NORMAL);   /* ★ 清掉坏标志, 免得每次开机都试 */
                    /* 不跳转 —— 外层的 if 已经进来了，不会再走下面的 CheckApp(A) 分支，
					  所以实际是"直接落到 for(;;) 等下一次升级"，安全但要重新按 Key1 */
                }
                else if (CopyBtoA(0) == RES_OK)
                {
                    Flag_SetState(SYS_STATE_NORMAL);
                    Serial_Printf("[BL]搬运成功, 跳转到 APP\r\n");
                    JumpToApp(APP_ADDR);
                }
                else
                {
                    Serial_Printf("[BL]搬运失败, 停在 BL (B 区完整, 重启会重试)\r\n");
                }
            }
            else if (CheckApp(APP_ADDR) == RES_OK)
            {
                Serial_Printf("[BL]未按键且 APP 有xiao → 跳转到 APP\r\n");
                JumpToApp(APP_ADDR);
            }
            else
            {
                Serial_Printf("[BL]APP 无xiao, 继续等待升级\r\n");
            }
        }
    }

    Serial_Printf("[BL]停在 BL, 等待升级\r\n");

    /* ★★★ CAN 升级接收(第1版): 认 START → 收 DATA 存进 g_fwBuf */
    for(;;)
    {
        CanFrame_t f;
        uint32_t   t0;
        uint32_t   totalLen  = 0;      /* START 报的总长度 */
        uint32_t   got       = 0;      /* 已收字节数 */
        uint8_t    upgrading = 0;      /* 0=还没收到 START */
        uint16_t   expectSeq = 0;      /* ★ 期望收到的下一帧序号 */
        uint16_t   lostFrames = 0;     /* ★ 累计丢了多少帧 */


        Serial_Printf("[CAN] 等待 START 帧...\r\n");
        t0 = HAL_GetTick();

        while ((HAL_GetTick() - t0) < 10000U)
        {
            if (CanRing_Get(&f) != RES_OK)    /* ★ 统一: RES_OK(0)=取到, RES_FAIL(1)=空 */
            {
                continue;    /* 没帧 → 继续等 */
            }

			/* ★★★ 不是发给我的帧 → 无视
			(数据帧 ID = 0x100 + 目标节点号; 我的 = 0x101)
			★ ACK 帧(0x2xx) 也在这里被一起挡掉 ? */
			if (f.id != (UP_ID_DATA_BASE + MY_NODE_ID))
			   continue;

            /* ① 先验 CRC8, 错了直接丢 */
            if (CAN_CalcCRC8(f.data, PROTO_POS_CRC) != f.data[PROTO_POS_CRC])
            {
                continue;
            }

            /* ② 按功能码分派 */
            if (f.data[PROTO_POS_FUNC] == FUNC_UP_START)
            {
                totalLen = (uint32_t)f.data[1]
                           | ((uint32_t)f.data[2] << 8)
                           | ((uint32_t)f.data[3] << 16)
                           | ((uint32_t)f.data[4] << 24);
                got       = 0;
                upgrading = 1;
                expectSeq = 0;        /* ★★★ 新一轮, 序号从 0 重新数 */
                lostFrames = 0;       /* ★ 新一轮, 丢帧计数也归零 */
                t0 = HAL_GetTick();                  /* ★ 收到帧 → 重置超时 */
                Serial_Printf("[CAN] START: 总长 %u 字节\r\n", (unsigned int)totalLen);
				Can_SendAck(RES_OK, 0U);    /* ★ 告诉母机: "START 收到, 我准备好了" */
            }

            else if (f.data[PROTO_POS_FUNC] == FUNC_UP_DATA && upgrading)
            {

                /* ★★★ 取出发来的帧序号（小端） */
                uint16_t seq = (uint16_t)(f.data[UP_POS_SEQ]
                                          | (f.data[UP_POS_SEQ + 1] << 8));

                /* ★★★ 和期望的比 —— 不等就是【中间丢帧了】 */
                if (seq != expectSeq)
                {
                    Serial_Printf("[CAN] ? 丢帧! 期望 %u 收到 %u (已收 %u 字节)\r\n",
                                  (unsigned int)expectSeq,
                                  (unsigned int)seq,
                                  (unsigned int)got);
                    lostFrames += (uint16_t)(seq - expectSeq);   /* ★ 记账 */
                }

                expectSeq = (uint16_t)(seq + 1);                 /* ★ 跟上/前进一格 */

                if (got + UP_DATA_LEN <= APP_MAX_SIZE)
                {
                    g_fwBuf[got + 0] = f.data[UP_POS_DATA + 0];
                    g_fwBuf[got + 1] = f.data[UP_POS_DATA + 1];
                    g_fwBuf[got + 2] = f.data[UP_POS_DATA + 2];
                    g_fwBuf[got + 3] = f.data[UP_POS_DATA + 3];
                    got += UP_DATA_LEN;
                }

                t0 = HAL_GetTick();                  /* ★ 收到帧 → 重置超时 */

                /* ★ 进度打印已移除: 每 1000 字节一行, 一次升级要 28 行,
                   会把"本轮结束/丢帧/校验结果"这些关键信息淹没。
                   需要看进度时, 看母机侧每 100 帧一次的 [UP-ACK] 即可。 */
				/* ★★★ 用 expectSeq(帧序号) 判断"每 100 帧" ——
					 丢帧时它会【跟上】(不跳跃), 所以 ACK 不会漏 ? */
				if ((expectSeq % 100U) == 0U && expectSeq > 0U)
				   Can_SendAck(RES_OK, expectSeq);
            }

            else if (f.data[PROTO_POS_FUNC] == FUNC_UP_END)
            {
                uint16_t totalFrames = (uint16_t)(f.data[1] | (f.data[2] << 8));

                Serial_Printf("[CAN] ★ 本轮结束: 共收 %u / %u 字节, 收 %u 帧, 期望 %u 帧, 丢 %u 帧\r\n",
                              (unsigned int)got, (unsigned int)totalLen,
                              (unsigned int)(got / UP_DATA_LEN),
                              (unsigned int)totalFrames,
                              (unsigned int)lostFrames);

                /* ★★★ 收全了吗? 没全 → 【一个字都不写 Flash】 */
                if (got != totalLen || totalLen == 0U)
                {
                    Serial_Printf("[CAN] ? 收不全(%u/%u), 本轮作废, 不写 Flash\r\n",
                                  (unsigned int)got, (unsigned int)totalLen);
					Can_SendAck(RES_FAIL, (uint16_t)(got / UP_DATA_LEN));   /* ★ 收不全 */
                    upgrading = 0;
                    break;                              /* ★ 回 for(;;) 等下一轮 */
                }

                /* ★★★ 收全了 → 擦 → 写 → 校验（复用串口版那套） */
                Serial_Printf("[CAN] 收全, 开始写 Flash...\r\n");

                if (Flash_WriteFirmware(got) == RES_OK)
                {
                    Serial_Printf("[CAN] B 区写入并验证成功! (%u 字节)\r\n", (unsigned int)got);
                    /* ★ 这里【不发 ACK】—— 等搬运完再发, 否则母机会误报成功 */

                    /* ★★★ B 区就绪 → 立刻搬到 A 区
                           为什么不能直接跳 B 区: 固件是按【A 区地址】链接的,
                           在 B 区地址上跑, 所有绝对地址都会错位 */
                    Serial_Printf("[CAN] 开始搬运 B → A...\r\n");

                    if (CopyBtoA(got) == RES_OK)
                    {
                        Flag_SetState(SYS_STATE_NORMAL);     /* ★ 搬完清标志 */

                        /* ★★★ 到这里才算真正成功 → 才回 ACK */
                        Can_SendAck(RES_OK, (uint16_t)(got / UP_DATA_LEN));

                        Serial_Printf("[CAN] 搬运成功, 50ms 后跳转...\r\n");
                        HAL_Delay(50);
                        JumpToApp(APP_ADDR);                 /* ★ 跳 A 区 */
                    }
                    else
                    {
                        /* ★ 搬运失败: 回"失败", 停在 BL 别跳
                           B 区是完整的 → 重启后开机会自动重试搬运 */
                        Serial_Printf("[CAN] 搬运失败! 停在 BL (B 区完整, 重启自动重试)\r\n");
                        Can_SendAck(RES_FAIL, (uint16_t)(got / UP_DATA_LEN));
                    }
				}
                else
                {
                    Serial_Printf("[CAN] ? 写入或校验失败\r\n");
					Can_SendAck(RES_FAIL, (uint16_t)(got / UP_DATA_LEN));   /* ★ 写失败 */
                }

                upgrading = 0;
                break;                                  /* ★ 回 for(;;) 等下一轮 */
            }

            /* 其他功能码(心跳 0x01 等) → 落到这里, 什么都不做 = 无视 ? */
        }

        Serial_Printf("[CAN] 窗口结束: 共收 %u 字节, 期望序号 %u, 丢 %u 帧\r\n",
                      (unsigned int)got, (unsigned int)expectSeq, (unsigned int)lostFrames);
    }

  /* USER CODE END 2 */

//  /* Init scheduler */
//  osKernelInitialize();

//  /* Call init function for freertos objects (in cmsis_os2.c) */
//  MX_FREERTOS_Init();

//  /* Start scheduler */
//  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1)
    {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    }

  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM1 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM1) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();

    while (1)
    {
    }

  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number,
       ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
