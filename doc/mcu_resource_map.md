# RA8P1 / RX71M 外设资源对应

两款 MCU 按同一套数量配置。应用入口都是 FreeRTOS 任务里的 `board_app_run()`，每 500 ms 轮询一次：翻转 GPIO、采样 AD、更新 DA 与 PWM、SPI 传 1 字节、IIC 写 1 字节、发一帧 CAN、发 LIN Break+`0x55`，并把状态打到日志串口。业务串口收到的字节原样回发。

| 项目 | RA8P1 | RX71M |
|---|---|---|
| 器件 | R7JA8P1KSLSAJ（303-pin BGA） | R5F571MLDxLC（177-pin TFLGA） |
| 工程 | `POC_RA8P` | `POC_RX71M` |
| 代码 | `POC_RA8P/src/board/board_resources.cpp` | `POC_RX71M/src/board/board_resources.cpp` |
| 调用点 | `new_thread0_entry()` | `main_task()` |
| CAN | CAN FD（CANFD0、CANFD1） | 经典 CAN（CAN0、CAN1） |

JTAG/SWD 仍占用 RA8P1 的 P208–P211。

## 资源对照

| 资源 | 数量 | RA8P1 | RX71M |
|---|---|---|---|
| USB | 1 | USB FS 设备。DP=P814，DM=P815，VBUS=P407，P500 拉低。时钟 HOCO 48 MHz | USB0 设备，使用专用 DP/DM。UCLK = PLL 240 MHz / 5 = 48 MHz |
| 日志 UART | 1 | SCI8，TX=PD02，RX=PD03，115200 8N1 | SCI1，TX=P26，RX=P30，115200 8N1 |
| 业务 UART | 1 | SCI7，TX=P809，RX=P808，115200 8N1 | SCI2，TX=P50，RX=P52，115200 8N1 |
| CAN | 2 | CANFD0：CTX=P313，CRX=P312。CANFD1：CTX=P609，CRX=P610。500 kbit/s | CAN0：CTX=P32，CRX=P33。CAN1：CTX=P54，CRX=P55。500 kbit/s |
| SPI | 1 | SPI1：SCK=P102，MOSI=P101，MISO=P100，SSL=P103 | RSPI0：SCK=PA5，MOSI=PA6，MISO=PA7，SSL=PA4 |
| IIC | 1 | IIC0：SCL=P400，SDA=P401，100 kHz | RIIC0：SCL=P12，SDA=P13，100 kHz |
| LIN | 1 | SCI2：TX=P801，RX=P802，19200 | SCI5：TX=PC3，RX=PC2，19200 |
| GPIO 输入 | 4 | P008、P009、P012、P410，内部上拉 | P05、P07、PE0、PE1。P05/P07 内部上拉 |
| GPIO 输出 | 4 | P303、P600、PA07、P409 | P90、P91、P92、P93 |
| AD 输入 | 2 | P001=AN001，P007=AN007。ADC_B 扫描组 0 | P40=AN000，P41=AN001。S12AD |
| DA 输出 | 1 | P014=DA0。DAC_B0，12-bit | DA0，12-bit，DAOE0 |
| PWM | 4 | 1 kHz。GPT1A=P105，GPT1B=P104，GPT12A=P501，GPT10A=P810 | 1 kHz。MTIOC3A=P14，MTIOC3B=P17，MTIOC4A=P24，MTIOC4C=P25 |
| 以太网 | 1 | RGMII。TXD3–0=P304–P307，TX_CTL=P310，TX_CLK=P309，RXD0–3=P906–P909，RX_CTL=P206，RX_CLK=P905，MDC=P415，MDIO=P414，PHY 复位=P708 | RMII0。MDC=P71，MDIO=P72，REF50CK=P76，RX_ER=P77，TXD1=P80，RXD0=P81，TX_EN=P82，TXD0=P83，RXD1=P86，CRS_DV=P87。MAC `02:00:00:00:00:01` |

## 上电日志

日志串口先打印一行：

- `RA8P1 resources up`
- `RX71M resources up`

之后每 500 ms 一行，格式为 `t=`、`adc=`、`in=`。

## 板级说明

- RA8P1 引脚按 EK-RA8P1 已公开的复用关系选取。自定义板按上表端口接线。
- 使用 EK-RA8P1 时，SW4-3 需拨到 Octo-SPI 断开，否则 P808 以及 SPI/PWM 那组脚会被 OSPI 占用。
- RX71M 这颗封装的 MPC 没有 P84/P85，RMII 的 RXD1、CRS_DV 放在 P86、P87。

## 当前代码做到的范围

寄存器完成时钟、引脚和基本收发。USB 打开设备上拉。以太网完成引脚、MAC 地址和 EDMAC 复位。USB CDC、TCP/IP、CAN 滤波和 CANlog 还要加在这一层上面。
