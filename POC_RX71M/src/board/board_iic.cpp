/* RX71M 板级。RIIC0 主机。 */
#include "board_priv.hpp"

namespace board
{


/* RIIC0 主机，约 100 kHz。先复位再放行。 */
void iic_open(void)
{
    RIIC0.ICCR1.BIT.ICE = 0U;    /* 内部时钟先关。 */
    RIIC0.ICCR1.BIT.IICRST = 1U; /* 进入复位。 */
    RIIC0.ICCR1.BIT.ICE = 1U;    /* 复位期间打开时钟。 */
    RIIC0.ICMR1.BIT.CKS = 3U;    /* 时钟分频档。 */
    RIIC0.ICBRH.BYTE = 36U;      /* SCL 高电平计数。 */
    RIIC0.ICBRL.BYTE = 36U;      /* SCL 低电平计数。 */
    RIIC0.ICFER.BIT.NFE = 1U;    /* 数字噪声滤波。 */
    RIIC0.ICCR1.BIT.IICRST = 0U; /* 退出复位。 */
}


/* 向 7 位地址写一个数据字节。失败时尽量发停止位。 */
bool iic_write(uint8_t addr, uint8_t data)
{
    RIIC0.ICSR2.BYTE = static_cast<unsigned char>(RIIC0.ICSR2.BYTE & static_cast<unsigned char>(~0x10U)); /* 清 NACK。 */
    RIIC0.ICCR2.BYTE = 0x62U; /* 起始条件，并按主机发送启动。 */
    if (!wait_true(RIIC0.ICSR2.BYTE, 0x80U, true, 100000U)) /* 等起始完成。 */
    {
        return false;
    }
    RIIC0.ICDRT = static_cast<unsigned char>(addr << 1); /* 地址左移，最低位 0 表示写。 */
    if (!wait_true(RIIC0.ICSR2.BYTE, 0x40U, true, 100000U)) /* 等该字节发完。 */
    {
        RIIC0.ICCR2.BIT.SP = 1U; /* 停止位，释放总线。 */
        return false;
    }
    if ((RIIC0.ICSR2.BYTE & 0x10U) != 0U) /* 从机 NACK。 */
    {
        RIIC0.ICCR2.BIT.SP = 1U;
        return false;
    }
    RIIC0.ICDRT = data; /* 数据字节。 */
    (void) wait_true(RIIC0.ICSR2.BYTE, 0x40U, true, 100000U);
    RIIC0.ICCR2.BIT.SP = 1U; /* 停止位。 */
    return true;
}

} // namespace board
