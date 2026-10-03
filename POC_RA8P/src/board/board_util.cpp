/* RA8P 板级。寄存器位等待。 */
#include "board_priv.hpp"

namespace board
{


/* 轮询 32 位寄存器的某一位，直到变成期望电平或次数耗尽。 */
bool wait_flag(volatile const uint32_t & reg, uint32_t mask, bool set, uint32_t spins)
{
    while (spins-- != 0U)                 /* 每次循环消耗一次等待额度。 */
    {
        const bool on = (reg & mask) != 0U; /* 只看 mask 对应的位。 */
        if (on == set)                      /* 已是期望的 0 或 1。 */
        {
            return true;
        }
    }
    return false;                           /* 超时，调用方决定是否放弃。 */
}


/* 与 wait_flag 相同，对象是 8 位状态寄存器。 */
bool wait_u8(volatile const uint8_t & reg, uint8_t mask, bool set, uint32_t spins)
{
    while (spins-- != 0U)
    {
        const bool on = (reg & mask) != 0U;
        if (on == set)
        {
            return true;
        }
    }
    return false;
}

} // namespace board
