/* RX71M 板级。模块停止位：解锁、给时钟、再锁上。 */
#include "board_priv.hpp"

namespace board
{


/* 解开模块停止寄存器的写保护。0xA502：PRC1=1，并带写入键。 */
void mstp_unlock(void)
{
    SYSTEM.PRCR.WORD = 0xA502U;
}


/* 重新锁上。0xA500 只留写入键，保护位清 0。 */
void mstp_lock(void)
{
    SYSTEM.PRCR.WORD = 0xA500U;
}


/* 写 0 表示退出模块停止，让本文件用到的外设开始给时钟。 */
void modules_start(void)
{
    mstp_unlock();
    MSTP_SCI1 = 0U;   /* 日志串口。 */
    MSTP_SCI2 = 0U;   /* 业务串口。 */
    MSTP_SCI5 = 0U;   /* LIN。 */
    MSTP_CAN0 = 0U;   /* 经典 CAN 通道 0。 */
    MSTP_CAN1 = 0U;   /* 经典 CAN 通道 1。RX71M 没有 CAN FD。 */
    MSTP_RSPI0 = 0U;  /* SPI。 */
    MSTP_RIIC0 = 0U;  /* I2C。 */
    MSTP_USB0 = 0U;   /* USB FS 设备。 */
    MSTP_EDMAC0 = 0U; /* 以太网 DMA，ETHERC 随它一起用。 */
    MSTP_S12AD = 0U;  /* 12 位 ADC。 */
    MSTP_DA = 0U;     /* DAC。 */
    MSTP_MTU = 0U;    /* 多功能定时器，用来出 PWM。 */
    mstp_lock();
}

} // namespace board
