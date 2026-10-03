# 工作区目录与 MCU 资源

本文说明仓库里各目录放什么，并列出 RX71M 与 RA8P1 的存储器、时钟、片上模块，以及当前固件实际占用。引脚接线对照在 `mcu_resource_map.md`。任务和看门狗的软件位置在 `software_architecture.md`。

数字来自工程里的 BSP、链接脚本和本次编译的 `size` 输出，不是芯片手册的全系列上限。

## 1. 顶层目录

| 路径 | 内容 |
|---|---|
| `PC_Host` | Lazarus 4.8 上位机。窗体、串口、HL1 与 proto3 的 Pascal 实现，以及 `tools/hl_check` |
| `protocol` | 帧和 proto3 的定义。`host_link.proto`、`host_link.c`、`selftest.c`、`samples` |
| `POC_RX71M` | RX71M 固件。Smart Configurator、RX GCC、FreeRTOS |
| `POC_RA8P` | RA8P1 固件。FSP、Arm LLVM、FreeRTOS |
| `doc` | 需求、协议、架构、测试、工数、本文 |
| `ci` | 本地流水线 `pipeline.ps1`。产物在 `ci/out`，不进版本库 |
| `.github/workflows` | 可选的 GitHub Actions。公司环境不用 GitHub 时可以不跑这些文件 |
| `.metadata` | e2 studio 工作区缓存，不进版本库 |

根目录的 `Renesas_RX71M_RA8P.code-workspace` 是编辑器工作区文件。

## 2. 目录树

```text
Renesas_RX71M_RA8P
├── PC_Host
│   ├── pc_host.lpi / pc_host.lpr
│   ├── src            窗体、DPI、串口、协议
│   └── tools          hl_check
├── protocol
│   ├── host_link.proto / host_link.h / host_link.c / selftest.c
│   └── samples
├── POC_RX71M
│   ├── src/board      应用代码，按外设分文件
│   ├── src/frtos_config
│   ├── src/smc_gen    厂商生成代码，不改业务逻辑
│   ├── Debug / Release / HardwareDebug
│   └── makefile.targets
├── POC_RA8P
│   ├── src/board
│   ├── ra / ra_gen / ra_cfg
│   ├── Debug / Release
│   └── makefile.targets
├── doc
└── ci
    └── pipeline.ps1
```

`Debug`、`Release` 里的 `subdir.mk` 是命令行编译用的生成文件。在 e2 studio 里刷新工程后，若 IDE 重新生成 makefile，要确认 `src/board` 下的 cpp 仍在源文件列表里。

## 3. 文档索引

| 文件 | 语言 | 内容 |
|---|---|---|
| `embedded_system_requirements.md` / `_ja.md` | 中 / 日 | 委托需求，不是当前实现状态 |
| `host_link_protocol.md` / `_ja.md` | 中 / 日 | HL1 与 protobuf 字段 |
| `protocol_change_workflow.md` | 中文 | 改协议时要动哪些文件 |
| `software_architecture.md` / `_ja.md` | 中 / 日 | 已落地的软件结构、FreeRTOS、看门狗 |
| `system_test_design.md` / `_ja.md` | 中 / 日 | 系统测试与工数 |
| `project_backlog.md` / `_ja.md` | 中 / 日 | 后续故事与排期。旁附同名 xlsx |
| `firmware_image_and_renesas_lineup.md` | 中文 | HEX/BIN 产物，以及系列定位 |
| `mcu_resource_map.md` | 中文 | 引脚与当前用到的外设对照 |
| `workspace_layout.md` | 中文 | 本文 |

## 4. 固件应用代码

两边的 `src/board` 都是自由函数，公共声明在 `board_priv.hpp`，对任务公开的入口仍是 `board_app_run()`。

| 文件 | 作用 |
|---|---|
| `board_app.cpp` | `bringup()`、500 ms 的 `poll()`、20 ms 任务循环 |
| `board_link.cpp` | 协议回调，并编入 `protocol/host_link.c` |
| `board_wdt.cpp` | 独立看门狗 |
| `board_clock.cpp` / `board_pins.cpp` | 模块时钟、引脚 |
| `board_sci.cpp` / `board_spi.cpp` / `board_iic.cpp` | UART、SPI、I2C、LIN |
| `board_can.cpp` / `board_pwm.cpp` | CAN、PWM |
| `board_adc.cpp` / `board_dac.cpp` / `board_gpio.cpp` | 采样与 GPIO |
| `board_usb.cpp` / `board_eth.cpp` | USB、以太网的模块打开 |
| `board_log.cpp` / `board_state.cpp` / `board_util.cpp` | 日志行、状态、小工具 |

## 5. 存储器

### 5.1 芯片与链接脚本给出的区域

| 区域 | RX71M `R5F571MLDxLC` | RA8P1（本工程链接脚本） |
|---|---|---|
| 代码存储 | 代码闪存 4,194,304 字节。`BSP_CFG_MCU_PART_MEMORY_SIZE = 0x15` | 代码区 1,048,576 字节，起始 `0x02000000`。FSP 标记为 MRAM |
| 数据非易失 | 数据闪存 65,536 字节 | 链接脚本 `DATA_FLASH_LENGTH = 0`，本工程没有数据闪存区 |
| 片内 RAM | 524,288 字节 | 系统 RAM `0x22000000`，长度 `0x001D4000`（1,900,544 字节） |
| 紧耦合 RAM | 无此项 | ITCM、DTCM 各 131,072 字节。当前镜像未放入 |
| 其他映射 | 无 | SiP 闪存窗口 8 MiB、SDRAM 窗口 128 MiB、OSPI 两片各 256 MiB。当前镜像都未使用 |
| 封装 | 177-pin TFLGA | 303-pin BGA，`R7JA8P1KSLSAJ` |

### 5.2 当前镜像占用

`rx-elf-size` / `llvm-size` 的 berkeley 格式。text 加 data 落在代码区，data 加 bss 落在 RAM。RX 的 8 KiB 堆在 bss 里，应用任务栈从这 8 KiB 里分配。

| 镜像 | text | data | bss | 代码区占用 | RAM 静态占用 |
|---|---:|---:|---:|---|---|
| RX71M Debug 与 Release | 27,972 | 1,024 | 16,124 | 28,996 / 4,194,304（0.7%） | 17,148 / 524,288（3.3%） |
| RA8P Debug | 20,834 | 0 | 8,532 | 20,834 / 1,048,576（2.0%） | 8,532 / 1,900,544（0.4%） |
| RA8P Release | 39,518 | 8 | 8,548 | 39,526 / 1,048,576（3.8%） | 8,556 / 1,900,544（0.5%） |

数据闪存、ITCM、DTCM、SiP、SDRAM、OSPI 窗口的占用都是 0。

## 6. 时钟

### 6.1 RX71M

晶振 24 MHz，PLL 不分频再乘 10，得到 240 MHz。

| 时钟 | 频率 | 本工程用途 |
|---|---|---|
| ICLK | 240 MHz | CPU。`configCPU_CLOCK_HZ` |
| PCLKA | 120 MHz | 外设时钟 A |
| PCLKB | 60 MHz | SCI、CAN、RSPI、RIIC、MTU。FreeRTOS 节拍用 CMT0，PCLKB 再除 8 |
| PCLKC | 60 MHz | 外设时钟 C |
| PCLKD | 60 MHz | 外设时钟 D |
| FCLK | 60 MHz | 闪存接口 |
| BCLK | 120 MHz | 外部总线时钟，引脚未输出 |
| UCLK | 48 MHz | USB，PLL 240 MHz 除 5 |
| IWDTCLK | 标称 15 kHz | 独立看门狗专用振荡，不走 PCLKB |

### 6.2 RA8P1

晶振 24 MHz。PLL 先除 3 再乘 250，得到 2 GHz。PLL1P 再除 2，得到 1 GHz，作为系统时钟源。PLL2 关闭。HOCO 为 48 MHz。

| 时钟 | 频率 | 本工程用途 |
|---|---|---|
| CPUCLK | 1 GHz | Cortex-M85 |
| CPUCLK1 | 250 MHz | 第二 CPU 时钟分频，应用未单独使用 |
| NPUCLK | 500 MHz | NPU 时钟分频，应用未使用 NPU |
| ICLK | 250 MHz | 外设总线 |
| PCLKA | 125 MHz | SCI、SPI。板级常量 `kPclkaHz` |
| PCLKB | 62.5 MHz | 分频已配置 |
| PCLKC | 125 MHz | 分频已配置 |
| PCLKD | 250 MHz | GPT。板级常量 `kPclkdHz` |
| PCLKE | 250 MHz | 分频已配置 |
| FCLK / MRPCLK | 125 MHz | 分频已配置 |
| BCLK | 125 MHz | 分频已配置，未接到引脚 |
| CANFDCLK | HOCO 48 MHz | CAN FD 位时钟的时钟源配置 |
| IWDT 时钟 | 16,384 Hz | FSP `BSP_FEATURE_IWDT_CLOCK_FREQUENCY` |

## 7. 片上模块与当前使用

“片上”按本工程头文件里实际出现的通道统计。“当前”只算应用代码已经打开的部分。

### 7.1 RX71M

| 模块 | 片上 | 当前 |
|---|---|---|
| SCI | SCI0–SCI7、SCI12，共 9 路 | SCI1 日志 115200，SCI2 业务 HL1，SCI5 LIN 19200 |
| CAN | CAN0–CAN2 | CAN0、CAN1 经典发送，500 kbit/s。无接收邮箱 |
| RSPI | RSPI0、RSPI1 | RSPI0，500 ms 交换 1 字节 |
| RIIC | RIIC0、RIIC2 | RIIC0，100 kHz，向 `0x50` 写 1 字节 |
| USB | USB0 | 设备时钟与 D+ 上拉。无 USB-CDC |
| 以太网 | ETHERC0、ETHERC1 | ETHERC0，RMII，MAC `02:00:00:00:00:01`，收发使能。无报文 |
| 12 位 ADC | S12AD、S12AD1 | S12AD 的 AN000、AN001 |
| 12 位 DAC | DA0 | 使用，上电为 0 |
| MTU | MTU0–MTU8 | MTU3、MTU4，1 kHz PWM 四路 |
| CMT | CMT0–CMT3 | CMT0 作 FreeRTOS 1 ms 节拍 |
| IWDT | 1 | 软件启动，超时约 17.5 s |
| WDT | 1 | 未启动 |
| TMR、RTC、QSPI、SDHI、PDC | 各有 | 未用 |
| DMAC、EXDMAC、DTC | 有 | 未用 |

PWM 占空比写在 `MTU3.TGRB`、`MTU4.TGRB`、`MTU4.TGRC`、`MTU4.TGRD`，对应 MTIOC3B、MTIOC4B、MTIOC4C、MTIOC4D。`mcu_resource_map.md` 的接线表写的是 MTIOC3A、MTIOC3B、MTIOC4A、MTIOC4C。MTIOC4B 与 MTIOC4D 的引脚不在该表里。

### 7.2 RA8P1

| 模块 | 片上 | 当前 |
|---|---|---|
| SCI | 掩码 `0x03FF`，SCI0–SCI9 共 10 路 | SCI7 业务，SCI8 日志，SCI2 LIN |
| SPI | 2 路 | SPI1，500 ms 交换 1 字节 |
| IIC | 掩码 `0x07`，3 路 | IIC0，100 kHz，向 `0x50` 写 1 字节 |
| I3C | 1 路 | 未用 |
| CAN FD | 2 个实例 | CANFD0、CANFD1 发送。`fd=true` 时置 FD 格式位 |
| USB | FS 与 HS | FS 设备时钟与 D+ 上拉。无 USB-CDC |
| 以太网 | 2 路 | 一路 RGMII 引脚与 PHY 复位。无报文 |
| ADC_B | 2 个单元 | 单元 0 的 AN001、AN007 |
| DAC_B | 2 个单元，每单元 1 通道 | DAC_B0 |
| GPT | 32 位通道掩码 `0x3FFF`，14 路 | GPT1A、GPT1B、GPT12A、GPT10A，1 kHz |
| DMAC | 8 通道 | 未用 |
| IWDT | 1，支持寄存器启动 | 软件启动，超时 16 s |
| WDT | 1 | 未启动 |
| OSPI_B | 2 单元 | 未用 |
| SDHI | 2 路 | 未用 |
| SSI | 掩码 `0x03`，2 路 | 未用 |
| RTC、CRC、DOC、ELC、MIPI CSI、MIPI DSI | 有 | 未用 |

## 8. 看门狗

两边都只启动独立看门狗 IWDT。选项字里的启动位保持为停，复位后不会自己跑起来。`bringup()` 做完时钟、引脚和外设之后才写 `IWDTCR`，这一写开始计数，控制寄存器之后不能再改。

| 项目 | RX71M | RA8P1 |
|---|---|---|
| 代码 | `POC_RX71M/src/board/board_wdt.cpp` | `POC_RA8P/src/board/board_wdt.cpp` |
| 时钟 | 标称 15 kHz | 16,384 Hz |
| 分频 / 计数 | /16，16,384 次 | 相同 |
| 超时 | 约 17.5 s | 16 s |
| 窗口 | 起点 100%，终点 0% | 相同 |
| 下溢 | 复位整片 | 复位整片 |
| 喂狗 | 任务每 20 ms，先写 `0x00` 再写 `0xFF` | 相同 |

睡眠时计数不停止。应用任务若卡住超过上述时间，芯片复位，日志口会再次打印 `resources up`。
