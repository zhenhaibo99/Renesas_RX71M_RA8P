/* RA8P 板级。IIC0 主机。 */
#include "board_priv.hpp"

namespace board
{


/* IIC0 主机，约 100 kHz。先复位模块再放行。 */
void iic_open(void)
{
    R_IIC0->ICCR1_b.ICE = 0U;     /* 内部时钟先关。 */
    R_IIC0->ICCR1_b.IICRST = 1U;  /* 进入复位。 */
    R_IIC0->ICCR1_b.ICE = 1U;     /* 复位期间打开时钟。 */
    R_IIC0->ICMR1_b.CKS = 3U;     /* 时钟分频档。 */
    R_IIC0->ICBRH = 38U;          /* SCL 高电平计数。 */
    R_IIC0->ICBRL = 38U;          /* SCL 低电平计数。 */
    R_IIC0->ICMR3 = 0x01U;        /* 噪声滤波等基本设置。 */
    R_IIC0->ICFER = 0x30U;        /* 打开主机需要的功能位。 */
    R_IIC0->ICCR1_b.IICRST = 0U;  /* 退出复位，开始工作。 */
}


/* 向 7 位地址写一个数据字节。失败时尽量发出停止位。 */
bool iic_write(uint8_t addr, uint8_t data)
{
    R_IIC0->ICSR2 = static_cast<uint8_t>(R_IIC0->ICSR2 & static_cast<uint8_t>(~0x10U)); /* 清 NACK。 */
    R_IIC0->ICCR2 = 0x62U; /* 起始条件，并按主机发送启动。 */
    if (!wait_u8(R_IIC0->ICSR2, 0x80U, true, 100000U)) /* 等起始条件完成。 */
    {
        return false;
    }
    R_IIC0->ICDRT = static_cast<uint8_t>(addr << 1); /* 地址左移，最低位 0 表示写。 */
    if (!wait_u8(R_IIC0->ICSR2, 0x40U, true, 100000U)) /* 等发送结束。 */
    {
        R_IIC0->ICCR2_b.SP = 1U; /* 发停止位，释放总线。 */
        return false;
    }
    if ((R_IIC0->ICSR2 & 0x10U) != 0U) /* 从机 NACK。 */
    {
        R_IIC0->ICCR2_b.SP = 1U;
        return false;
    }
    R_IIC0->ICDRT = data; /* 数据字节。 */
    (void) wait_u8(R_IIC0->ICSR2, 0x40U, true, 100000U);
    R_IIC0->ICCR2_b.SP = 1U; /* 停止位。 */
    return true;
}

} // namespace board
