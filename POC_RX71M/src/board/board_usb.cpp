/* RX71M 板级。USB FS 设备时钟和 D+ 上拉。 */
#include "board_priv.hpp"

namespace board
{


/* USB0 设备：先供 48 MHz 时钟，再打开模块并拉起 D+。 */
void usb_open(void)
{
    USB0.SYSCFG.WORD = static_cast<unsigned short>(USB0.SYSCFG.WORD | static_cast<unsigned short>(1U << 10)); /* bit10 = SCKE。 */
    volatile uint32_t spins = 8000U; /* 等 USB 时钟稳定。用 volatile 防止被优化掉。 */
    while (spins-- != 0U)
    {
    }
    USB0.SYSCFG.WORD = static_cast<unsigned short>(USB0.SYSCFG.WORD | static_cast<unsigned short>((1U << 0) | (1U << 4))); /* bit0=USBE，bit4=DPRPU。 */
}

} // namespace board
