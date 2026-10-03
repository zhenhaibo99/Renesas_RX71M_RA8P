/* RA8P 板级。以太网 DMA 复位。 */
#include "board_priv.hpp"

namespace board
{


/* 以太网 DMA 软件复位，等复位位自己清掉。这里只做模块活着，不发帧。 */
void eth_open(void)
{
    R_ETHERC_EDMAC->EDMR_b.SWR = 1U;
    (void) wait_flag(R_ETHERC_EDMAC->EDMR, 1UL, false, 200000U);
}

} // namespace board
