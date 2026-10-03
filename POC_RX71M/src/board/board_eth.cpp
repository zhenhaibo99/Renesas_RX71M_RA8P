/* RX71M 板级。以太网 DMA 复位和 MAC。 */
#include "board_priv.hpp"

namespace board
{


/* 以太网：DMA 软件复位，写一个本地 MAC，全双工并打开收发。这里不组帧。 */
void eth_open(void)
{
    EDMAC0.EDMR.BIT.SWR = 1U; /* 软件复位。 */
    uint32_t spins = 200000U;
    while ((EDMAC0.EDMR.BIT.SWR != 0U) && (spins-- != 0U)) /* 复位位会自己清掉。 */
    {
    }
    ETHERC0.MAHR = 0x02000000UL;       /* MAC 高 32 位：02:00:00:00。 */
    ETHERC0.MALR.LONG = 0x00000001UL;  /* MAC 低 16 位：00:01。 */
    ETHERC0.ECMR.BIT.DM = 1U;          /* 全双工。 */
    ETHERC0.ECMR.BIT.RE = 1U;          /* 接收使能。 */
    ETHERC0.ECMR.BIT.TE = 1U;          /* 发送使能。 */
}

} // namespace board
