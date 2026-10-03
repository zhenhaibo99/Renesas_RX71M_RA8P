/* RX71M 板级。经典 CAN。 */
#include "board_priv.hpp"

namespace board
{


/* 等 CAN 进入或离开复位。in_reset 为真表示要看到 RSTST=1。 */
bool can_wait_reset(volatile struct st_can & can, bool in_reset)
{
    uint32_t spins = 200000U;
    while (spins-- != 0U)
    {
        if ((can.STR.BIT.RSTST != 0U) == in_reset)
        {
            return true;
        }
    }
    return false;
}


/* 经典 CAN：退出睡眠，进复位，设 500 kbit/s，再回到工作模式。 */
void can_open(volatile struct st_can & can)
{
    can.CTLR.BIT.SLPM = 0U; /* 退出睡眠模式。 */
    can.CTLR.BIT.CANM = 1U; /* 进入复位，才能改位定时。 */
    if (!can_wait_reset(can, true))
    {
        return;
    }
    can.BCR.BIT.CCLKS = 0U; /* 位时钟选 PCLKB，不是外部时钟。 */
    can.BCR.BIT.BRP = 11U;  /* 分频 12。60 MHz / 12 = 5 MHz。 */
    can.BCR.BIT.TSEG1 = 5U; /* 时间段 1 为 6 个 Tq。 */
    can.BCR.BIT.TSEG2 = 2U; /* 时间段 2 为 3 个 Tq。加上同步段共 10 Tq。 */
    can.BCR.BIT.SJW = 0U;   /* 同步跳转宽度 1 Tq。5 MHz / 10 = 500 kbit/s。 */
    can.CTLR.BIT.CANM = 0U; /* 回到正常工作。 */
    (void) can_wait_reset(can, false);
}


/* 用邮箱 0 发一帧 11 位标准 ID、1–8 字节。邮箱忙或参数非法则失败。 */
bool can_send(volatile struct st_can & can, uint16_t id, const uint8_t *data, uint8_t dlc)
{
    uint8_t i;
    if ((data == 0) || (dlc == 0U) || (dlc > 8U) || (id > 0x7FFU))
    {
        return false;
    }
    if (can.MCTL[0].BIT.TX.TRMACTIVE != 0U) /* 邮箱 0 还在发送。 */
    {
        return false;
    }
    can.MCTL[0].BYTE = 0U; /* 清邮箱控制，才能改 ID 和数据。 */
    /* 标准帧 ID 在 SID[10:0]，位于 ID 寄存器 bit28-18。直接写整字，避免 11 位位域的截断警告。 */
    can.MB[0].ID.LONG = (static_cast<unsigned long>(id) & 0x7FFUL) << 18;
    can.MB[0].DLC = dlc; /* 数据长度。 */
    for (i = 0U; i < dlc; ++i)
    {
        can.MB[0].DATA[i] = data[i];
    }
    can.MCTL[0].BYTE = 0x80U; /* TRMREQ，请求发送。 */
    return true;
}

} // namespace board
