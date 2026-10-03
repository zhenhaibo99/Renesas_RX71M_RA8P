/* RX71M 板级。SCI 串口和 LIN Break。 */
#include "board_priv.hpp"

namespace board
{


/* 波特率分频。CKS=0 再除 1，否则再除 4。BGDM=1 时按 8 倍过采样。BRR = PCLKB/(div*8*baud) - 1。 */
uint8_t sci_brr(uint32_t baud, uint32_t cks)
{
    const uint32_t div = (cks == 0U) ? 1U : 4U;   /* 只使用 CKS 的 0 和 1 两档。 */
    const uint32_t den = div * 8U * baud;         /* 分母。 */
    uint32_t brr = (kPclkbHz + (den / 2U)) / den; /* 四舍五入。 */
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


/* 打开一路 SCI：8 位数据，无校验，1 停止位，收发都使能。 */
void sci_open(volatile struct st_sci0 * sci, uint32_t baud, uint32_t cks)
{
    sci->SCR.BYTE = 0U; /* 先关收发，才能改模式和波特率。 */
    sci->SMR.BYTE = static_cast<unsigned char>(cks & 0x03U); /* 低 2 位是 CKS，其余为异步 8N1。 */
    sci->SCMR.BYTE = 0xF2U; /* 智能卡模式关闭，保持 8 位数据。 */
    sci->SEMR.BIT.BGDM = 1U; /* 波特率发生器加倍，配合上面的 8 倍公式。 */
    sci->BRR = sci_brr(baud, cks);
    sci->SCR.BIT.RE = 1U; /* 接收使能。 */
    sci->SCR.BIT.TE = 1U; /* 发送使能。 */
}


/* 等发送缓冲空再写一个字节。超时就丢弃，避免卡死任务。 */
void sci_putc(volatile struct st_sci0 * sci, uint8_t ch)
{
    if (!wait_true(sci->SSR.BYTE, 0x80U, true, 200000U)) /* SSR.TDRE = bit7。 */
    {
        return;
    }
    sci->TDR = ch;
}


/* 发送 C 字符串，不含结尾 0。只给日志口用。 */
void sci_write(volatile struct st_sci0 * sci, const char * text)
{
    while (*text != '\0')
    {
        sci_putc(sci, static_cast<uint8_t>(*text));
        ++text;
    }
}


/* 读一个字节。没有数据返回 -1。0x00 是合法二进制，不能当结束符。 */
int sci_getc(volatile struct st_sci0 * sci)
{
    if ((sci->SSR.BYTE & 0x38U) != 0U) /* bit5:3 = ORER/FER/PER，接收出错。 */
    {
        sci->SSR.BYTE = static_cast<unsigned char>(sci->SSR.BYTE & static_cast<unsigned char>(~0x38U)); /* 清错误位。 */
        (void) sci->RDR; /* 再读一次接收寄存器，把残留数据丢掉。 */
    }
    if ((sci->SSR.BYTE & 0x40U) == 0U) /* bit6 = RDRF，接收数据满。 */
    {
        return -1;
    }
    return static_cast<int>(sci->RDR);
}


/* LIN Break：TX 临时改成 GPIO 拉低约 1 ms，再交还 SCI，发出同步字节 0x55。 */
void lin_break_and_sync(void)
{
    kLinUart->SCR.BIT.TE = 0U;   /* 先关发送，避免和 GPIO 抢脚。 */
    PORTC.PMR.BIT.B3 = 0U;       /* PC3 退出 SCI，回到端口。 */
    PORTC.PDR.BIT.B3 = 1U;       /* 输出。 */
    PORTC.PODR.BIT.B3 = 0U;      /* 拉低，这就是 Break。 */
    R_BSP_SoftwareDelay(1U, BSP_DELAY_MILLISECS);
    PORTC.PODR.BIT.B3 = 1U;      /* 释放为高，Break 结束。 */
    PORTC.PMR.BIT.B3 = 1U;       /* 交还 SCI5。 */
    kLinUart->SCR.BIT.TE = 1U;   /* 重新打开发送。 */
    sci_putc(kLinUart, 0x55U);   /* LIN 同步字段。 */
}

} // namespace board
