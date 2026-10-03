/* RX71M 板级。RSPI0 主机。 */
#include "board_priv.hpp"

namespace board
{


/* RSPI0 主机，8 位，波特率分频 SPBR=29。 */
void spi_open(void)
{
    RSPI0.SPCR.BYTE = 0U;    /* 关闭后再改模式。 */
    RSPI0.SPPCR.BYTE = 0U;   /* 不用环回。 */
    RSPI0.SPBR = 29U;        /* 波特率分频。 */
    RSPI0.SPDCR.BYTE = 0U;   /* 单帧，不用长数据。 */
    RSPI0.SPCMD0.BIT.SPB = 7U; /* 数据长度 8 位。 */
    RSPI0.SPCR.BIT.MSTR = 1U;  /* 主机。 */
    RSPI0.SPCR.BIT.SPE = 1U;   /* 启动 SPI。 */
}


/* 交换一个字节。发送空或接收满若超时，返回值可能是旧数据。 */
uint8_t spi_xfer(uint8_t tx)
{
    uint32_t spins = 100000U;
    while ((RSPI0.SPSR.BIT.SPTEF == 0U) && (spins-- != 0U)) /* 等发送缓冲空。 */
    {
    }
    RSPI0.SPDR.LONG = tx; /* 8 位写入数据寄存器的低位。 */
    spins = 100000U;
    while ((RSPI0.SPSR.BIT.SPRF == 0U) && (spins-- != 0U)) /* 等接收缓冲满。 */
    {
    }
    return static_cast<uint8_t>(RSPI0.SPDR.LONG);
}

} // namespace board
