/* RA8P 板级。GPT PWM。 */
#include "board_priv.hpp"

namespace board
{


/* 启动一路 GPT。both 为真时 A/B 两相都输出，B 相占空比是 A 的一半。 */
void gpt_start(R_GPT0_Type * gpt, uint32_t channel, uint32_t duty, bool both)
{
    gpt->GTWP = 0xA500U;            /* 写保护解锁键。 */
    R_GPT0->GTSTP = 1UL << channel; /* 先停这个通道。GTSTP 在公共寄存器。 */
    gpt->GTCR = 0U;                 /* 锯齿波向上计数。 */
    gpt->GTPR = (kPclkdHz / kPwmHz) - 1U; /* 1 kHz 的周期。 */
    gpt->GTCCR[0] = duty;           /* A 相比较值。 */
    uint32_t ior = 6UL | (1UL << 8); /* A 相输出模式，并打开 A 相输出。 */
    if (both)
    {
        gpt->GTCCR[1] = duty / 2U;          /* B 相用一半占空比。 */
        ior |= (6UL << 16) | (1UL << 24);   /* 同样打开 B 相。 */
    }
    gpt->GTIOR = ior;
    gpt->GTWP = 0xA500U;            /* 再次写入键，保持后面还能改比较值。 */
    R_GPT0->GTSTR = 1UL << channel; /* 启动计数。 */
}


/* 按千分比改某一路 PWM。0–3 对应 GPT1A、GPT1B、GPT12A、GPT10A。 */
int apply_pwm(uint8_t channel, uint16_t duty_permille)
{
    uint32_t cmp;
    if ((g_pwm_period == 0U) || (channel > 3U) || (duty_permille > 1000U))
    {
        return -1; /* 周期还没建好，或参数越界。 */
    }
    cmp = (g_pwm_period * duty_permille) / 1000U; /* 千分比换成比较值。 */
    if (cmp > g_pwm_period)
    {
        cmp = g_pwm_period; /* 100% 时不超过周期寄存器。 */
    }
    switch (channel)
    {
    case 0U:
        R_GPT1->GTWP = 0xA500U;    /* 解锁 GPT1。 */
        R_GPT1->GTCCR[0] = cmp;    /* A 相。 */
        break;
    case 1U:
        R_GPT1->GTWP = 0xA500U;
        R_GPT1->GTCCR[1] = cmp;    /* B 相。 */
        break;
    case 2U:
        R_GPT12->GTWP = 0xA500U;
        R_GPT12->GTCCR[0] = cmp;
        break;
    default:
        R_GPT10->GTWP = 0xA500U;
        R_GPT10->GTCCR[0] = cmp;   /* 通道 3。 */
        break;
    }
    return 0;
}

} // namespace board
