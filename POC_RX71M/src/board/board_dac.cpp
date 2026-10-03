/* RX71M 板级。DA0。 */
#include "board_priv.hpp"

namespace board
{


/* 写 12 位 DAC，右对齐，并打开通道 0 模拟输出。 */
void dac_write(uint16_t code)
{
    g_dac = static_cast<uint16_t>(code & 0x0FFFU); /* 只留 12 位，供状态上报。 */
    DA.DADPR.BIT.DPSEL = 1U; /* 数据右对齐。 */
    DA.DADR0 = g_dac;
    DA.DACR.BIT.DAOE0 = 1U;  /* DA0 输出使能。 */
}

} // namespace board
