/* RA8P 板级。DAC_B0。 */
#include "board_priv.hpp"

namespace board
{


/* DAC_B0：右对齐，允许输出并打开转换。 */
void dac_open(void)
{
    R_DAC_B0->DACR1_b.DPSEL = 1U;   /* 数据右对齐。 */
    R_DAC_B0->DADR = 0U;            /* 上电输出 0。 */
    R_DAC_B0->DACR0_b.DAOUTDIS = 0U; /* 不禁止模拟输出。 */
    R_DAC_B0->DACR0_b.DACEN = 1U;   /* 使能 DAC。 */
}


/* 写入 12 位码，并记住给上位机状态用。 */
void dac_write(uint16_t code)
{
    g_dac = static_cast<uint16_t>(code & 0x0FFFU); /* 只留 12 位。 */
    R_DAC_B0->DADR = g_dac;
}

} // namespace board
