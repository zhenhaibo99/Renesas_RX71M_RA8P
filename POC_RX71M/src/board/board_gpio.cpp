/* RX71M 板级。四路输入和四路输出。 */
#include "board_priv.hpp"

namespace board
{


/* 按掩码写 P90–P93，并记住低 4 位。 */
void gpio_write(uint8_t mask)
{
    g_outputs = static_cast<uint8_t>(mask & 0x0FU);
    mask = g_outputs;
    PORT9.PODR.BIT.B0 = (mask & 0x01U) != 0U; /* P90。 */
    PORT9.PODR.BIT.B1 = (mask & 0x02U) != 0U; /* P91。 */
    PORT9.PODR.BIT.B2 = (mask & 0x04U) != 0U; /* P92。 */
    PORT9.PODR.BIT.B3 = (mask & 0x08U) != 0U; /* P93。 */
}


/* 读四路输入，合成 bit0–bit3。 */
uint8_t gpio_read(void)
{
    uint8_t value = 0U;
    if (PORT0.PIDR.BIT.B5 != 0U) /* P05。 */
    {
        value = static_cast<uint8_t>(value | 0x01U);
    }
    if (PORT0.PIDR.BIT.B7 != 0U) /* P07。 */
    {
        value = static_cast<uint8_t>(value | 0x02U);
    }
    if (PORTE.PIDR.BIT.B0 != 0U) /* PE0。 */
    {
        value = static_cast<uint8_t>(value | 0x04U);
    }
    if (PORTE.PIDR.BIT.B1 != 0U) /* PE1。 */
    {
        value = static_cast<uint8_t>(value | 0x08U);
    }
    return value;
}

} // namespace board
