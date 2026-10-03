/* RA8P 板级。USB FS 设备。 */
#include "board_priv.hpp"

namespace board
{


/* USB FS 设备：供时钟、选设备模式、打开模块并拉起 D+。 */
void usb_open(void)
{
    R_USB_FS0->SYSCFG_b.SCKE = 1U; /* USB 时钟使能。 */
    R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MILLISECONDS);
    R_USB_FS0->SYSCFG_b.DCFM = 0U;  /* 0 = 设备，不是主机。 */
    R_USB_FS0->SYSCFG_b.DRPD = 0U;  /* 设备模式不接下拉。 */
    R_USB_FS0->SYSCFG_b.USBE = 1U;  /* 打开 USB 模块。 */
    R_USB_FS0->SYSCFG_b.DPRPU = 1U; /* D+ 上拉，向主机表明全速设备。 */
}

} // namespace board
