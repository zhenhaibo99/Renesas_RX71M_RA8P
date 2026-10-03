/* RA8P 板级。SPI1 主机。 */
#include "board_priv.hpp"

namespace board
{


/* SPI1 主机，8 位，波特率分频 SPBR=61。 */
void spi_open(void)
{
    R_SPI1->SPCR = 0U;   /* 关闭后再改模式。 */
    R_SPI1->SPPCR = 0U;  /* 不用环回等测试模式。 */
    R_SPI1->SPBR = 61U;  /* 波特率分频。 */
    R_SPI1->SPSCR = 0U;  /* 只用命令寄存器 0。 */
    R_SPI1->SPCMD[0] = static_cast<uint16_t>(7U << 8); /* 数据长 8 位。 */
    R_SPI1->SPCR = static_cast<uint8_t>((1U << 3) | (1U << 6)); /* 主机，并启动 SPI。 */
}


/* 交换一个字节。发送空或接收满超时则返回 0。 */
uint8_t spi_xfer(uint8_t tx)
{
    if (!wait_u8(R_SPI1->SPSR, static_cast<uint8_t>(1U << 5), true, 100000U)) /* 发送缓冲空。 */
    {
        return 0U;
    }
    R_SPI1->SPDR_BY = tx;
    if (!wait_u8(R_SPI1->SPSR, static_cast<uint8_t>(1U << 7), true, 100000U)) /* 接收缓冲满。 */
    {
        return 0U;
    }
    return R_SPI1->SPDR_BY;
}

} // namespace board
