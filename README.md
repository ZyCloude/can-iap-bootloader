# CAN 总线多节点 IAP 远程固件升级系统

[English](#english) | 中文

基于 **STM32F407 + FreeRTOS** 的 CAN 多节点远程固件升级方案，打通
**PC 串口下发 → 母机缓存 → CAN 总线传输 → 子机双区写入 → 自动跳转新固件** 的完整闭环，
解决现场设备无法拆机烧录的问题。

> ⚠️ 本项目是 **IAP（In-Application Programming）**，走 CAN **有线**总线。
> 不是 OTA —— OTA 是 IAP 的无线分支，底层能力相同，差别在传输层的可靠性设计。

---

## 📊 实测数据

| 指标 | 结果 |
|---|---|
| 固件大小 | **28,392 字节** |
| CAN 帧数 | **7,098 帧**（每帧 4 字节有效载荷） |
| **丢帧** | **0** |
| 擦除耗时 | **0.52 s**（优化前 **7.4 s**，快 14 倍） |
| 单轮升级耗时 | ≈ 40 s（含擦除 + 写入 + 校验 + 搬运） |
| 连续升级 | 多轮稳定，无需复位 |
| **断电恢复** | 双区搬运中断电 → 重启自动重搬 ✓ |

---

## 🏗️ 系统架构

```
┌──────────┐   UART    ┌──────────────┐   CAN 500kbps   ┌──────────────┐
│    PC    │ ────────► │  母机 Master  │ ──────────────► │ 子机 Slave   │
│ send_fw.py│ 115200bps │  STM32F407   │   自定义协议     │  BootLoader  │
└──────────┘           │  FreeRTOS    │                 │  无 RTOS     │
                       └──────────────┘                 └──────────────┘
                              │                               │
                              ▼                               ▼
                       ┌──────────────┐              ┌────────────────┐
                       │ Flash 暂存区  │              │  A 区 (执行)    │
                       │ 0x08080000   │              │  0x08010000    │
                       └──────────────┘              ├────────────────┤
                                                     │  B 区 (暂存)    │
                                                     │  0x08080000    │
                                                     ├────────────────┤
                                                     │  标志区 (日志)  │
                                                     │  0x080E0000    │
                                                     └────────────────┘
```

### Flash 分区（F407，1 MB）

| 区域 | 地址 | 大小 | 扇区 |
|---|---|---|---|
| **BootLoader** | `0x08000000` | 64 KB | Sector 0~3（16 KB × 4） |
| **APP A 区**（执行） | `0x08010000` | **448 KB** | Sector 4~7（64 KB + 128 KB × 3） |
| **APP B 区**（暂存） | `0x08080000` | **384 KB** | Sector 8~10（128 KB × 3） |
| **标志区** | `0x080E0000` | 128 KB | Sector 11 |

> ⚠️ **Sector 4 是 64 KB，Sector 5~11 是 128 KB** —— 大小不统一，擦除计算必须逐扇区累加。

---

## ✨ 核心特性

### 1. 自定义 IAP 协议（四类帧）

| 功能码 | 名称 | 载荷 |
|---|---|---|
| `0x10` | `START` | 固件总字节数 |
| `0x11` | `DATA` | 帧序号(2B) + 4 字节固件 |
| `0x12` | `END` | 总帧数 |
| `0x13` | `ACK` | 状态(1B) + 已收帧数(2B) |

- CAN ID = `0x100 + 目标节点号`，支持多节点
- **每帧 4 字节数据 = 正好一个 Flash WORD**，写入无需拼接

### 2. 三层校验体系

| 层级 | 机制 | 覆盖的失效点 |
|---|---|---|
| **帧级** | CRC8（CAN 载荷） | CAN 总线位翻转、ID 错配 |
| **帧序** | 序号连续性检查 | 丢帧、乱序 |
| **整包** | CRC32（PC 计算，母机验证） | 串口传输错误、上位机读文件错误 |

> 💡 **为什么不一刀切都用 CRC32？**
> CAN 自身有硬件 CRC15 + 自动重传，再加一层收益极低；
> 而 UART 是**唯一没有硬件校验的一跳**，所以 CRC32 只加在串口段。

### 3. A/B 双区回滚 + 断电自愈

```
升级流程：
  ① 新固件写入 B 区
  ② B 区读回校验
  ③ 通过后 → 写魔数 + 长度 → 置"待搬运"标志
  ④ 搬运到 A 区
  ⑤ 校验 A 区 → 写 A 区魔数 → 清标志 → 跳转
```

**断电保护设计：**

| 断电时机 | 后果 | 恢复方式 |
|---|---|---|
| 写 B 区中途 | B 区不完整 | 魔数未写 → 判无效 → 重新升级 |
| **搬运到 A 区中途** | A 区被部分擦写 | 标志仍为"待搬运" → **重启自动重搬** ✓ |
| 搬运完成、清标志前 | 标志残留 | 重启再搬一次（幂等，无害） |

**日志式标志区**：128 槽 × 1 KB，只追加不回头改。
按每次升级写 2 槽计算，**64 次升级才擦一次**，大幅降低标志区磨损。

### 4. Flash 擦除优化（7.4 s → 0.52 s）

```c
/* 优化前：固定擦 8 个扇区 */
NbSectors = 8;

/* 优化后：按固件实际长度逐扇区累加 */
/* Sector 4 = 64KB，Sector 5~11 = 128KB，不能简单除法 */
```

28 KB 固件只需擦 **1 个扇区（Sector 4）**，耗时从 7.4 s 降到 **0.52 s**。

### 5. 并发与可靠性

| 问题 | 方案 |
|---|---|
| 串口接收任务擦写 Flash 与 CAN 发送任务读取并发 | **FreeRTOS 互斥量**（非递归，所有出口必须 Release） |
| 母机未就绪导致 START 丢帧 | **START 帧自动重试 3 次**（约 7.5 s） |
| 总线异常导致 CAN 卡死 | **Bus-Off 检测 + Stop/Start 强制恢复** |
| 上位机传输出错 | **CRC32 + `--corrupt` 故障注入测试** |

---

## 🚀 快速开始

### 硬件

- 2 块 STM32F407 开发板（母机 + 子机）
- CAN 收发器（TJA1050 / SN65HVD230 等）
- 120 Ω 终端电阻 × 2（两端各一个）
- USB-TTL 串口模块（连接 PC 与母机）

### 编译

> ⚠️ **仓库不含 `Drivers/` 和 `Middlewares/`**（CubeMX 生成，体积大）。

```
1. 用 STM32CubeMX 打开各工程目录下的 Ncar.ioc
2. 点击 GENERATE CODE 重新生成驱动库
3. 用 Keil MDK 打开 MDK-ARM/Ncar.uvprojx 编译
```

**IROM1 配置（关键）：**

| 工程 | Start | Size |
|---|---|---|
| BootLoader (`bootloader/`) | `0x08000000` | `0x10000` |
| APP (`app/`) | `0x08010000` | `0x000F0000` |

### 烧录与升级

```
① 烧录 bootloader 到子机（0x08000000）
② 烧录 app 到子机（0x08010000），或留空
③ 烧录 master 到母机
④ PC 端运行下发脚本：
     python tools/send_fw.py COM5
⑤ 子机上电 → 10 秒内按 Key1 进入升级模式
⑥ 母机按 Key1 → 开始传输
```

### 故障注入测试

```bash
# 翻转固件中间 1 个字节，验证 CRC32 能否拦住
python tools/send_fw.py COM5 --corrupt
```

**预期输出：**

```
[PC] CRC32: 本地=23C822E9 收到=40D58329
[PC] ✗ CRC32 不匹配! 数据传输出错, 拒绝写 Flash
```

---

## 📁 目录结构

```
can-iap-bootloader/
├── bootloader/          子机 BootLoader（A/B 双区回滚，无 RTOS）
│   ├── Core/
│   │   ├── Inc/          can.h（协议定义）、main.h
│   │   └── Src/          main.c（擦写/搬运/跳转）、can.c（环形缓冲）
│   ├── Hardware/         Key / OLED
│   └── MDK-ARM/          Keil 工程
├── master/              母机（PC 下发 + CAN 转发，FreeRTOS 4 任务）
├── app/                 APP 端（跑在 0x08010000）
└── tools/
    └── send_fw.py       PC 上位机（串口下发 + CRC32 + 故障注入）
```

---

## 🛠️ 技术栈

| 类别 | 内容 |
|---|---|
| MCU | STM32F407ZGTx（168 MHz，1 MB Flash，128 KB RAM） |
| RTOS | FreeRTOS 10.x（CMSIS-RTOS2 封装），母机 4 任务 |
| 外设 | CAN 2.0B / UART / TIM / Flash / GPIO |
| 工具 | STM32CubeMX、Keil MDK 5、Python 3 + pyserial |
| 上位机 | Python（串口协议、CRC32、故障注入） |

---

## 📄 License

MIT

---

<a name="english"></a>
## English

**CAN Multi-Node IAP Firmware Update System** on STM32F407 + FreeRTOS.

Complete pipeline: **PC (UART) → Master (cache to Flash) → CAN bus → Slave (A/B dual-bank write) → auto jump to new firmware**.

**Key features:**
- Custom IAP protocol (START / DATA / END / ACK), 4-byte payload per CAN frame = 1 Flash WORD
- **3-layer verification**: frame CRC8 + sequence continuity + whole-image CRC32
- **A/B dual-bank rollback** with power-loss recovery (journal-style flag area, 128 slots)
- **Erase optimization**: 7.4 s → 0.52 s (14× faster) by per-sector size calculation
- **0 frame loss** over 7,098 CAN frames (28,392-byte firmware)
