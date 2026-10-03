/* RX71M 板级。MTU3/MTU4 PWM。 */
#include "board_priv.hpp"

namespace board
{


/* MTU3/MTU4 PWM 模式 1，计数时钟不分频。周期 60000 个 PCLKB，即 1 kHz。 */
void pwm_open(void)
{
    MTU.TSTRA.BIT.CST3 = 0U; /* 停 MTU3。 */
    MTU.TSTRA.BIT.CST4 = 0U; /* 停 MTU4。 */
    MTU3.TCR.BIT.CCLR = 1U;  /* TGRA 比较匹配时清零，TGRA 就是周期。 */
    MTU3.TCR.BIT.TPSC = 0U;  /* 不分频，60 MHz。 */
    MTU3.TMDR1.BIT.MD = 2U;  /* PWM 模式 1。 */
    MTU3.TIORH.BIT.IOA = 1U; /* MTIOC3A：初始输出低，匹配后变高。 */
    MTU3.TIORH.BIT.IOB = 2U; /* MTIOC3B：匹配后变低。 */
    MTU3.TGRA = 59999U;      /* 周期 = 60000 / 60 MHz = 1 ms。 */
    MTU3.TGRB = 30000U;      /* 约 50% 占空比，对应 P17。 */
    MTU3.TCNT = 0U;

    MTU4.TCR.BIT.CCLR = 1U;
    MTU4.TCR.BIT.TPSC = 0U;
    MTU4.TMDR1.BIT.MD = 2U;
    MTU4.TIORH.BIT.IOA = 1U; /* MTIOC4A = P24。 */
    MTU4.TIORL.BIT.IOC = 1U; /* MTIOC4C = P25。 */
    MTU4.TGRA = 59999U;      /* 同样 1 kHz。 */
    MTU4.TGRB = 15000U;      /* 约 25%。 */
    MTU4.TGRC = 7500U;       /* 约 12.5%，P25。 */
    MTU4.TCNT = 0U;

    MTU4.TIORL.BIT.IOD = 1U; /* 再打开 MTIOC4D，给第 4 路占空比。 */
    MTU4.TGRD = 3750U;       /* 约 6.25%。 */
    MTU.TOERA.BIT.OE3B = 1U; /* 允许 MTIOC3B 输出到引脚。 */
    MTU.TOERA.BIT.OE4A = 1U; /* MTIOC4A。 */
    MTU.TOERA.BIT.OE4C = 1U; /* MTIOC4C。 */
    MTU.TOERA.BIT.OE4D = 1U; /* MTIOC4D。 */
    MTU.TSTRA.BIT.CST3 = 1U; /* 启动 MTU3。 */
    MTU.TSTRA.BIT.CST4 = 1U; /* 启动 MTU4。 */
}


/* 千分比换成 MTU 比较值。周期固定 60000，100% 时停在 59999，避免等于清零点。 */
uint16_t duty_count(uint16_t duty_permille)
{
    const uint32_t period = 60000U;
    uint32_t cmp = (period * duty_permille) / 1000U;
    if (cmp >= period)
    {
        cmp = period - 1U;
    }
    return static_cast<uint16_t>(cmp);
}


/* 改四路 PWM 比较值。0=MTU3.TGRB，1=MTU4.TGRB，2=MTU4.TGRC，3=MTU4.TGRD。 */
int apply_pwm(uint8_t channel, uint16_t duty_permille)
{
    const uint16_t cmp = duty_count(duty_permille);
    switch (channel)
    {
    case 0U:
        MTU3.TGRB = cmp; /* P17 / MTIOC3B。 */
        break;
    case 1U:
        MTU4.TGRB = cmp; /* MTIOC4B。 */
        break;
    case 2U:
        MTU4.TGRC = cmp; /* P25 / MTIOC4C。 */
        break;
    case 3U:
        MTU4.TGRD = cmp; /* MTIOC4D。 */
        break;
    default:
        return -1; /* 只有 0–3。 */
    }
    return 0;
}

} // namespace board
