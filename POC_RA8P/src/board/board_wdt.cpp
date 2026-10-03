/* RA8P 独立看门狗 IWDT。FSP 给出的专用时钟是 16384 Hz，不走 PCLK。 */
#include "board_priv.hpp"

namespace board
{

/* 分频 /16，超时计数 16384。16384 Hz 下正好 16 s。窗口全开。
 * 写 IWDTCR 后控制寄存器不能再改。
 */
constexpr uint16_t kIwdtCr = static_cast<uint16_t>((3U << 12) | (3U << 8) | (2U << 4) | 3U);

/* 刷新计数。必须先写 0x00，再写 0xFF。 */
void iwdt_refresh(void)
{
    R_IWDT->IWDTRR = 0x00U;
    R_IWDT->IWDTRR = 0xFFU;
}


/* 选项字 OFS0 的 IWDT 启动位为停。写 IWDTCR 后开始计数。 */
void iwdt_open(void)
{
    R_IWDT->IWDTRCR_b.RSTIRQS = 1U; /* 下溢复位。 */
    R_IWDT->IWDTCSTPR_b.SLCSTP = 0U; /* 睡眠时继续计数。 */
    R_IWDT->IWDTCR = kIwdtCr;
    iwdt_refresh();
}

} // namespace board
