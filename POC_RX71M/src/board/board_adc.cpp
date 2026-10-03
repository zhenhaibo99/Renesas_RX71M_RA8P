/* RX71M 板级。S12AD 两路采样。 */
#include "board_priv.hpp"

namespace board
{


/* 单次扫描 AN000 和 AN001，等转换结束再读 12 位结果。 */
void adc_sample(void)
{
    S12AD.ADANSA0.WORD = 0x0003U; /* bit0、bit1：AN000、AN001。 */
    S12AD.ADCSR.BIT.ADCS = 0U;    /* 单次扫描，不是连续。 */
    S12AD.ADCSR.BIT.ADST = 1U;    /* 开始转换。 */
    uint32_t spins = 200000U;
    while ((S12AD.ADCSR.BIT.ADST != 0U) && (spins-- != 0U)) /* ADST 变 0 表示转完。 */
    {
    }
    g_adc0 = static_cast<uint16_t>(S12AD.ADDR0 & 0x0FFFU);
    g_adc1 = static_cast<uint16_t>(S12AD.ADDR1 & 0x0FFFU);
}

} // namespace board
