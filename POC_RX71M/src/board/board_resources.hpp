#ifndef BOARD_RESOURCES_HPP_
#define BOARD_RESOURCES_HPP_

#include <stdint.h>

/* RX71M (R5F571MLDxLC, 177-pin TFLGA) 与 RA8P1 数量相同的资源。
 * RX71M 的 CAN 是经典 CAN，没有 CAN FD。
 *
 * USB FS x1     USB0 专用 DP/DM，设备模式，UCLK = 48 MHz
 * UART 日志     SCI1  TX=P26 RX=P30   115200 8N1
 * UART 业务     SCI2  TX=P50 RX=P52   115200 8N1
 * CAN x2        CAN0 CTX=P32 CRX=P33；CAN1 CTX=P54 CRX=P55  500 kbit/s
 * SPI x1        RSPI0 RSPCK=PA5 MOSI=PA6 MISO=PA7 SSL=PA4
 * IIC x1        RIIC0 SCL=P12 SDA=P13  100 kHz
 * LIN x1        SCI5  TX=PC3 RX=PC2  19200，周期发送 Break+0x55
 * GPIO 输入 x4  P05 P07 PE0 PE1（内部上拉）
 * GPIO 输出 x4  P90 P91 P92 P93
 * AD 输入 x2    AN000=P40，AN001=P41（S12AD）
 * DA 输出 x1    DA0（12-bit，DAOE0）
 * PWM x4        MTIOC3A=P14，MTIOC3B=P17，MTIOC4A=P24，MTIOC4C=P25，1 kHz
 * Ethernet x1   ETHERC0/EDMAC0 RMII：
 *               MDC=P71 MDIO=P72 REF50CK=P76 RX_ER=P77
 *               TXD1=P80 RXD0=P81 TX_EN=P82 TXD0=P83 RXD1=P86 CRS_DV=P87
 */
/* FreeRTOS 的 main_task 调用这个函数，在里面初始化并循环。 */
extern "C" void board_app_run(void);

#endif
