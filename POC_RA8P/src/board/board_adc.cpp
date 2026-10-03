/* RA8P 板级。ADC_B 扫描组 0。 */
#include "board_priv.hpp"

namespace board
{


/* ADC_B：打开时钟，扫描组 0 选 AN001 和 AN007。 */
void adc_open(void)
{
    R_ADC_B->ADCLKENR_b.CLKEN = 1U; /* 打开 ADC 时钟。 */
    (void) wait_flag(R_ADC_B->ADCLKSR, 1UL, true, 200000U); /* 等时钟稳定。 */
    R_ADC_B->ADCLKCR = (2UL << 0) | (2UL << 16);            /* A/D 转换时钟分频。 */
    R_ADC_B->ADSSTR0 = (0x20UL << 0) | (0x20UL << 16);      /* 两路采样时间。 */
    R_ADC_B->ADCHCR0 = (1UL << 8);  /* 扫描组 0 的第一路：AN001。 */
    R_ADC_B->ADCHCR1 = (7UL << 8);  /* 第二路：AN007。 */
}


/* 启动一次扫描组 0，等 1 ms 后读结果。 */
void adc_sample(void)
{
    R_ADC_B->ADSTR[0] = 1U; /* 启动扫描组 0。 */
    R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MILLISECONDS);
    g_adc0 = static_cast<uint16_t>(R_ADC_B->ADDR[0] & 0xFFFFU); /* AN001。 */
    g_adc1 = static_cast<uint16_t>(R_ADC_B->ADDR[1] & 0xFFFFU); /* AN007。 */
}

} // namespace board
