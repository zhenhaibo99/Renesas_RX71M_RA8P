#ifndef BOARD_RESOURCES_HPP_
#define BOARD_RESOURCES_HPP_

#include <stdint.h>

/*
 * RA8P1（R7JA8P1KSLSAJ，303 脚）应用层资源表。
 * 引脚按 EK-RA8P1 已公开的复用关系选取，自制板按同一端口接线即可。
 * JTAG/SWD 仍占用 P208–P211，不在本表内。
 *
 * USB FS x1     USB_DP=P814  USB_DM=P815  VBUS=P407，设备模式，P500 拉低
 * UART 日志     SCI8  TX=PD02  RX=PD03   115200 8N1，只打文本
 * UART 业务     SCI7  TX=P809  RX=P808   115200 8N1，HL1 二进制帧
 * CAN FD x2     CANFD0 CTX=P313 CRX=P312；CANFD1 CTX=P609 CRX=P610，500 kbit/s
 * SPI x1        SPI1  RSPCK=P102 MOSI=P101 MISO=P100 SSL=P103
 * IIC x1        IIC0  SCL=P400  SDA=P401  100 kHz
 * LIN x1        SCI2  TX=P801  RX=P802   19200，周期发送 Break+0x55
 * GPIO 输入 x4  P008 P009 P012 P410（内部上拉）
 * GPIO 输出 x4  P303 P600 PA07 P409
 * AD 输入 x2    P001=AN001，P007=AN007（ADC_B 扫描组 0）
 * DA 输出 x1    P014=DA0（DAC_B0，12 位）
 * PWM x4        GPT1A=P105，GPT1B=P104，GPT12A=P501，GPT10A=P810，1 kHz
 * Ethernet x1   RGMII：TXD3-0=P304-P307，TX_CTL=P310，TX_CLK=P309
 *               RXD0-3=P906-P909，RX_CTL=P206，RX_CLK=P905
 *               MDC=P415 MDIO=P414，PHY 复位=P708
 */
/* FreeRTOS 线程入口最终调用这个函数；用 C 链接，避免名字被 C++ 改写。 */
extern "C" void board_app_run(void);

#endif
