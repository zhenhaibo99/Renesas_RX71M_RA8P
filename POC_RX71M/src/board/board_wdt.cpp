/* RX71M 独立看门狗 IWDT。专用振荡标称 15 kHz，不占用 PCLKB。 */
#include "board_priv.hpp"

namespace board
{

/* 分频 /16，超时计数 16384。15 kHz 下约 17.5 s。
 * 窗口起点 100%、终点 0%，20 ms 喂狗不会落在窗口外。
 * 这些位写进 IWDTCR 后计数开始，控制寄存器不能再改。
 */
constexpr unsigned short kIwdtCr = static_cast<unsigned short>((3U << 12) | (3U << 8) | (2U << 4) | 3U);

/* 刷新计数。必须先写 0x00，再写 0xFF。 */
void iwdt_refresh(void)
{
    IWDT.IWDTRR = 0x00U;
    IWDT.IWDTRR = 0xFFU;
}


/* OFS0 的 IWDTSTRT 为 1，复位后看门狗停着。写 IWDTCR 才开始。 */
void iwdt_open(void)
{
    IWDT.IWDTRCR.BIT.RSTIRQS = 1U; /* 下溢复位整片，而不是只请求中断。 */
    IWDT.IWDTCSTPR.BIT.SLCSTP = 0U; /* 睡眠时继续计数。 */
    IWDT.IWDTCR.WORD = kIwdtCr;
    iwdt_refresh(); /* 开始后立刻装载一次。 */
}

} // namespace board
