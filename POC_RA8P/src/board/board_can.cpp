/* RA8P 板级。CAN FD。 */
#include "board_priv.hpp"

namespace board
{


/* CAN FD 从睡眠进复位。全局和通道 0 都要做。失败返回 false。 */
bool canfd_leave_sleep(R_CANFD_Type * can)
{
    can->CFDGCTR_b.GSLPR = 0U; /* 全局退出睡眠。 */
    if (!wait_flag(can->CFDGSTS, 1UL << 2, false, 200000U)) /* 等全局睡眠标志清掉。 */
    {
        return false;
    }
    can->CFDGCTR_b.GMDC = 1U; /* 全局进入复位。 */
    if (!wait_flag(can->CFDGSTS, 1UL << 0, true, 200000U)) /* 等全局复位状态。 */
    {
        return false;
    }
    can->CFDC[0].CTR_b.CSLPR = 0U; /* 通道 0 退出睡眠。 */
    if (!wait_flag(can->CFDC[0].STS, 1UL << 2, false, 200000U))
    {
        return false;
    }
    can->CFDC[0].CTR_b.CHMDC = 1U; /* 通道 0 进入复位，这时才能改位定时。 */
    return wait_flag(can->CFDC[0].STS, 1UL << 0, true, 200000U);
}


/* 配置通道 0：仲裁段和数据段都按 500 kbit/s，然后进入工作模式。 */
void canfd_open(R_CANFD_Type * can)
{
    if (!canfd_leave_sleep(can))
    {
        return; /* 没进复位就不要写位定时。 */
    }
    /* 48 MHz / 6 = 8 MHz，16 Tq → 500 kbit/s。NBRP=5, NSJW=2, NTSEG1=10, NTSEG2=3。 */
    constexpr uint32_t ncfg = 5UL | (2UL << 10) | (10UL << 17) | (3UL << 25);
    /* 数据段用同一套分频和采样点，保证经典帧与 FD 的仲裁段一致。 */
    constexpr uint32_t dcfg = 5UL | (10UL << 8) | (3UL << 16) | (2UL << 24);
    can->CFDC[0].NCFG = ncfg;          /* 标称位定时。 */
    can->CFDC2[0].DCFG = dcfg;         /* 数据位定时。 */
    can->CFDGCTR_b.GMDC = 0U;          /* 全局进入工作。 */
    can->CFDC[0].CTR_b.CHMDC = 0U;     /* 通道进入工作。 */
    (void) wait_flag(can->CFDGSTS, 1UL << 0, false, 200000U); /* 等复位标志消失。 */
}


/* 用发送邮箱 0 发一帧 11 位标准 ID。fd 为真时置 CAN FD 格式位。邮箱忙则失败。 */
bool canfd_send(R_CANFD_Type * can, uint32_t id, const uint8_t * data, uint8_t dlc, bool fd)
{
    uint8_t i;
    if ((data == 0) || (dlc == 0U) || (dlc > 8U) || (id > 0x7FFU)) /* 本演示只做 1–8 字节、11 位 ID。 */
    {
        return false;
    }
    if ((can->CFDTMC[0] & 0x01U) != 0U) /* 邮箱 0 仍在发送。 */
    {
        return false;
    }
    can->CFDTM[0].ID = id << 18;                         /* 标准 ID 放在 ID 寄存器的高位。 */
    can->CFDTM[0].PTR = static_cast<uint32_t>(dlc) << 28; /* DLC 在指针寄存器的最高 4 位。 */
    can->CFDTM[0].FDCTR = fd ? (1UL << 2) : 0U;          /* bit2 = TMFDF，1 表示 CAN FD。 */
    for (i = 0U; i < dlc; ++i)
    {
        can->CFDTM[0].DF[i] = data[i]; /* 数据字节。 */
    }
    can->CFDTMC[0] = 0x01U; /* 请求发送。 */
    return true;
}

} // namespace board
