/* RA8P 板级。四路输入和四路输出。 */
#include "board_priv.hpp"

namespace board
{


/* 按掩码写四路输出，并保存掩码。只使用低 4 位。 */
void gpio_write(uint8_t mask)
{
    g_outputs = static_cast<uint8_t>(mask & 0x0FU);
    mask = g_outputs;
    const bsp_io_port_pin_t pins[4] = {
        BSP_IO_PORT_03_PIN_03, BSP_IO_PORT_06_PIN_00, BSP_IO_PORT_10_PIN_07, BSP_IO_PORT_04_PIN_09};
    R_BSP_PinAccessEnable();
    for (uint32_t i = 0U; i < 4U; ++i)
    {
        /* 该位为 1 则高，否则低。 */
        R_BSP_PinWrite(pins[i], ((mask & (1U << i)) != 0U) ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW);
    }
    R_BSP_PinAccessDisable();
}


/* 读四路输入，合成 bit0–bit3。 */
uint8_t gpio_read(void)
{
    const bsp_io_port_pin_t pins[4] = {
        BSP_IO_PORT_00_PIN_08, BSP_IO_PORT_00_PIN_09, BSP_IO_PORT_00_PIN_12, BSP_IO_PORT_04_PIN_10};
    uint8_t value = 0U;
    for (uint32_t i = 0U; i < 4U; ++i)
    {
        if (R_BSP_PinRead(pins[i]) != 0U)
        {
            value = static_cast<uint8_t>(value | static_cast<uint8_t>(1U << i));
        }
    }
    return value;
}

} // namespace board
