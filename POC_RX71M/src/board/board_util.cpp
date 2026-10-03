/* RX71M 板级。寄存器位等待。 */
#include "board_priv.hpp"

namespace board
{


/* 轮询 8 位寄存器的某些位，直到变成期望电平或次数耗尽。 */
bool wait_true(volatile const unsigned char & reg, unsigned char mask, bool set, uint32_t spins)
{
    while (spins-- != 0U)                 /* 每次循环消耗一次等待额度。 */
    {
        const bool on = (reg & mask) != 0U; /* 只看 mask 对应的位。 */
        if (on == set)                      /* 已经是期望的 0 或 1。 */
        {
            return true;
        }
    }
    return false;                           /* 超时。调用方决定是否放弃。 */
}

} // namespace board
