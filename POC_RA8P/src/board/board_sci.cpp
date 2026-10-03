/* RA8P 板级。SCI_B 串口和 LIN Break。 */
#include "board_priv.hpp"

namespace board
{


/* SCI 波特率分频。CKS=0/1/2 对应再除 1/4/16。BRR = PCLK/(div*8*baud) - 1，四舍五入。 */
uint8_t sci_brr(uint32_t baud, uint32_t cks)
{
    const uint32_t div = (cks == 0U) ? 1U : ((cks == 1U) ? 4U : 16U); /* 时钟分频。 */
    const uint32_t den = div * 8U * baud;                              /* 异步 8 倍过采样。 */
    uint32_t brr = (kPclkaHz + (den / 2U)) / den;                      /* 四舍五入。 */
    if (brr > 0U)
    {
        brr -= 1U; /* 硬件公式是 N-1。 */
    }
    if (brr > 255U)
    {
        brr = 255U; /* BRR 只有 8 位。 */
    }
    return static_cast<uint8_t>(brr);
}


/* 打开一路 SCI_B：8 位数据，无校验，1 停止位，收发都使能。 */
void sci_open(R_SCI_B0_Type * sci, uint32_t baud, uint32_t cks)
{
    sci->CCR0 = 0U; /* 先关掉，才能改波特率和格式。 */
    sci->CCR3 = (1UL << 8); /* 字符长度等格式位，按 8 位异步使用。 */
    /* bit4：波特率发生器相关使能；bit15:8：BRR；bit21:20：CKS。 */
    sci->CCR2 = (1UL << 4) | (static_cast<uint32_t>(sci_brr(baud, cks)) << 8) | (cks << 20);
    sci->CCR1 = (1UL << 28); /* 异步、无校验这一侧的控制位。 */
    sci->CCR0 = (1UL << 0) | (1UL << 4); /* bit0 接收使能，bit4 发送使能。 */
}


/* 等发送缓冲空，再写入一个字节。超时则丢弃，避免卡死任务。 */
void sci_putc(R_SCI_B0_Type * sci, uint8_t ch)
{
    if (!wait_flag(sci->CSR, 1UL << 29, true, 200000U)) /* CSR bit29：发送数据空。 */
    {
        return;
    }
    sci->TDR_BY = ch; /* 8 位发送数据寄存器。 */
}


/* 发送 C 字符串，不含结尾 0。用于日志口。 */
void sci_write(R_SCI_B0_Type * sci, const char * text)
{
    while (*text != '\0')
    {
        sci_putc(sci, static_cast<uint8_t>(*text));
        ++text;
    }
}


/* 读一个字节。没有数据返回 -1。二进制帧里的 0x00 是合法数据，不能当成结束。 */
int sci_getc(R_SCI_B0_Type * sci)
{
    if ((sci->CSR & (1UL << 24)) != 0U) /* 接收错误标志，读 RDR 把它清掉。 */
    {
        (void) sci->RDR_BY;
    }
    if ((sci->CSR & (1UL << 31)) == 0U) /* bit31：接收数据满。没有则返回。 */
    {
        return -1;
    }
    return static_cast<int>(sci->RDR_BY);
}


/* LIN Break：把 TX 强制拉低约 1 ms，再恢复 UART，发出同步字节 0x55。 */
void lin_break_and_sync(void)
{
    kLinUart->CCR1_b.SPB2DT = 0U; /* 单线输出数据选 0，也就是低电平。 */
    kLinUart->CCR1_b.SPB2IO = 1U; /* 改由该位直接驱动 TX，形成 Break。 */
    R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MILLISECONDS); /* Break 宽度。 */
    kLinUart->CCR1_b.SPB2IO = 0U; /* 交还 UART 移位器。 */
    sci_putc(kLinUart, 0x55U);    /* LIN 同步字段。 */
}

} // namespace board
