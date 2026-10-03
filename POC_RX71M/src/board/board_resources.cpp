#include "board_resources.hpp"

#include <cstring>

/* HL1 帧和 Protobuf 编解码，直接编进本文件。 */
#include "../../../protocol/host_link.c"

/* RX 寄存器头和 FreeRTOS 是 C 接口，用 C 链接包含。 */
extern "C"
{
#include "platform.h"
#include "FreeRTOS.h"
#include "task.h"
}

/* 本文件私有符号，避免和 BSP 全局名字冲突。 */
namespace
{
/* SCI、CAN、RSPI、RIIC、MTU 用的外设时钟 PCLKB。 */
constexpr uint32_t kPclkbHz = 60000000UL;
/* 日志口和业务口波特率。 */
constexpr uint32_t kUartBaud = 115200UL;
/* LIN 用较低波特率，便于 GPIO 拉出 Break。 */
constexpr uint32_t kLinBaud = 19200UL;

/* 日志文本：SCI1，TX=P26，RX=P30。 */
volatile struct st_sci0 * const kLogUart = &SCI1;
/* 与 PC 的 HL1 业务口：SCI2，TX=P50，RX=P52。 */
volatile struct st_sci0 * const kAppUart = &SCI2;
/* LIN：SCI5，TX=PC3，RX=PC2。 */
volatile struct st_sci0 * const kLinUart = &SCI5;

/* 约每 500 ms 加 1，同时当作演示数据。 */
uint32_t g_tick = 0U;
/* AN000 = P40 的最近一次采样。 */
uint16_t g_adc0 = 0U;
/* AN001 = P41 的最近一次采样。 */
uint16_t g_adc1 = 0U;
/* 四路输入，bit0–bit3 = P05、P07、PE0、PE1。 */
uint8_t g_inputs = 0U;
/* 四路输出掩码，bit0–bit3 = P90–P93。连上 PC 后由上位机接管。 */
uint8_t g_outputs = 0U;
/* DAC0 当前 12 位码。 */
uint16_t g_dac = 0U;
/* PC 链路。未收到合法帧前不往业务口发二进制。 */
HlBoardLink g_link;

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

/* 解开模块停止寄存器的写保护。0xA502：PRC1=1，并带写入键。 */
void mstp_unlock(void)
{
    SYSTEM.PRCR.WORD = 0xA502U;
}

/* 重新锁上。0xA500 只留写入键，保护位清 0。 */
void mstp_lock(void)
{
    SYSTEM.PRCR.WORD = 0xA500U;
}

/* 写 0 表示退出模块停止，让本文件用到的外设开始给时钟。 */
void modules_start(void)
{
    mstp_unlock();
    MSTP_SCI1 = 0U;   /* 日志串口。 */
    MSTP_SCI2 = 0U;   /* 业务串口。 */
    MSTP_SCI5 = 0U;   /* LIN。 */
    MSTP_CAN0 = 0U;   /* 经典 CAN 通道 0。 */
    MSTP_CAN1 = 0U;   /* 经典 CAN 通道 1。RX71M 没有 CAN FD。 */
    MSTP_RSPI0 = 0U;  /* SPI。 */
    MSTP_RIIC0 = 0U;  /* I2C。 */
    MSTP_USB0 = 0U;   /* USB FS 设备。 */
    MSTP_EDMAC0 = 0U; /* 以太网 DMA，ETHERC 随它一起用。 */
    MSTP_S12AD = 0U;  /* 12 位 ADC。 */
    MSTP_DA = 0U;     /* DAC。 */
    MSTP_MTU = 0U;    /* 多功能定时器，用来出 PWM。 */
    mstp_lock();
}

/* 把一根脚切到外设功能：先写 PFS 选择，再把 PMR 对应位设为外设。 */
void periph_pin(volatile unsigned char & pfs, unsigned char sel, volatile unsigned char & pmr, unsigned char bit)
{
    pfs = sel; /* MPC 的功能选择码。 */
    pmr = static_cast<unsigned char>(pmr | static_cast<unsigned char>(1U << bit)); /* 该位改由外设驱动。 */
}

/* 普通 GPIO 输出：PMR 清成端口，PDR 置 1 表示输出。 */
void gpio_dir_out(volatile unsigned char & pmr, volatile unsigned char & pdr, unsigned char bit)
{
    pmr = static_cast<unsigned char>(pmr & static_cast<unsigned char>(~(1U << bit)));
    pdr = static_cast<unsigned char>(pdr | static_cast<unsigned char>(1U << bit));
}

/* 普通 GPIO 输入：PMR、PDR 都清 0。 */
void gpio_dir_in(volatile unsigned char & pmr, volatile unsigned char & pdr, unsigned char bit)
{
    pmr = static_cast<unsigned char>(pmr & static_cast<unsigned char>(~(1U << bit)));
    pdr = static_cast<unsigned char>(pdr & static_cast<unsigned char>(~(1U << bit)));
}

/* 按资源表配置引脚。写 PFS 前要先打开 PWPR。 */
void pins_apply(void)
{
    MPC.PWPR.BIT.B0WI = 0U;  /* 先允许改 PFSWE。 */
    MPC.PWPR.BIT.PFSWE = 1U; /* 允许写各脚的 PFS。 */

    /* 0x0A = SCI。日志 SCI1，业务 SCI2。 */
    periph_pin(MPC.P26PFS.BYTE, 0x0AU, PORT2.PMR.BYTE, 6U); /* SCI1 TX = P26。 */
    periph_pin(MPC.P30PFS.BYTE, 0x0AU, PORT3.PMR.BYTE, 0U); /* SCI1 RX = P30。 */
    periph_pin(MPC.P50PFS.BYTE, 0x0AU, PORT5.PMR.BYTE, 0U); /* SCI2 TX = P50。 */
    periph_pin(MPC.P52PFS.BYTE, 0x0AU, PORT5.PMR.BYTE, 2U); /* SCI2 RX = P52。 */

    /* 0x10 = CAN。两路都是经典 CAN。 */
    periph_pin(MPC.P32PFS.BYTE, 0x10U, PORT3.PMR.BYTE, 2U); /* CAN0 CTX = P32。 */
    periph_pin(MPC.P33PFS.BYTE, 0x10U, PORT3.PMR.BYTE, 3U); /* CAN0 CRX = P33。 */
    periph_pin(MPC.P54PFS.BYTE, 0x10U, PORT5.PMR.BYTE, 4U); /* CAN1 CTX = P54。 */
    periph_pin(MPC.P55PFS.BYTE, 0x10U, PORT5.PMR.BYTE, 5U); /* CAN1 CRX = P55。 */

    /* 0x0D = RSPI0。 */
    periph_pin(MPC.PA5PFS.BYTE, 0x0DU, PORTA.PMR.BYTE, 5U); /* RSPCK = PA5。 */
    periph_pin(MPC.PA6PFS.BYTE, 0x0DU, PORTA.PMR.BYTE, 6U); /* MOSI = PA6。 */
    periph_pin(MPC.PA7PFS.BYTE, 0x0DU, PORTA.PMR.BYTE, 7U); /* MISO = PA7。 */
    periph_pin(MPC.PA4PFS.BYTE, 0x0DU, PORTA.PMR.BYTE, 4U); /* SSL = PA4。 */

    /* 0x0F = RIIC0。 */
    periph_pin(MPC.P12PFS.BYTE, 0x0FU, PORT1.PMR.BYTE, 2U); /* SCL = P12。 */
    periph_pin(MPC.P13PFS.BYTE, 0x0FU, PORT1.PMR.BYTE, 3U); /* SDA = P13。 */

    /* LIN 仍复用 SCI 功能号 0x0A。Break 时会临时改回 GPIO。 */
    periph_pin(MPC.PC3PFS.BYTE, 0x0AU, PORTC.PMR.BYTE, 3U); /* SCI5 TX = PC3。 */
    periph_pin(MPC.PC2PFS.BYTE, 0x0AU, PORTC.PMR.BYTE, 2U); /* SCI5 RX = PC2。 */

    /* 0x01 = MTU 输出。四路 PWM。 */
    periph_pin(MPC.P14PFS.BYTE, 0x01U, PORT1.PMR.BYTE, 4U); /* MTIOC3A = P14。 */
    periph_pin(MPC.P17PFS.BYTE, 0x01U, PORT1.PMR.BYTE, 7U); /* MTIOC3B = P17。 */
    periph_pin(MPC.P24PFS.BYTE, 0x01U, PORT2.PMR.BYTE, 4U); /* MTIOC4A = P24。 */
    periph_pin(MPC.P25PFS.BYTE, 0x01U, PORT2.PMR.BYTE, 5U); /* MTIOC4C = P25。 */

    /* 以太网 RMII。0x11 是 ETHERC，0x12 是 50 MHz 参考时钟。 */
    periph_pin(MPC.P71PFS.BYTE, 0x11U, PORT7.PMR.BYTE, 1U); /* MDC = P71。 */
    periph_pin(MPC.P72PFS.BYTE, 0x11U, PORT7.PMR.BYTE, 2U); /* MDIO = P72。 */
    periph_pin(MPC.P76PFS.BYTE, 0x12U, PORT7.PMR.BYTE, 6U); /* REF50CK = P76。 */
    periph_pin(MPC.P77PFS.BYTE, 0x11U, PORT7.PMR.BYTE, 7U); /* RX_ER = P77。 */
    periph_pin(MPC.P80PFS.BYTE, 0x11U, PORT8.PMR.BYTE, 0U); /* TXD1 = P80。 */
    periph_pin(MPC.P81PFS.BYTE, 0x11U, PORT8.PMR.BYTE, 1U); /* RXD0 = P81。 */
    periph_pin(MPC.P82PFS.BYTE, 0x11U, PORT8.PMR.BYTE, 2U); /* TX_EN = P82。 */
    periph_pin(MPC.P83PFS.BYTE, 0x11U, PORT8.PMR.BYTE, 3U); /* TXD0 = P83。 */
    periph_pin(MPC.P86PFS.BYTE, 0x11U, PORT8.PMR.BYTE, 6U); /* RXD1 = P86。 */
    periph_pin(MPC.P87PFS.BYTE, 0x11U, PORT8.PMR.BYTE, 7U); /* CRS_DV = P87。 */
    MPC.PFENET.BIT.PHYMODE0 = 0U; /* 0 = RMII，不是 MII。 */

    /* 四路数字输入，四路都开内部上拉。 */
    gpio_dir_in(PORT0.PMR.BYTE, PORT0.PDR.BYTE, 5U); /* P05，掩码 bit0。 */
    gpio_dir_in(PORT0.PMR.BYTE, PORT0.PDR.BYTE, 7U); /* P07，bit1。 */
    gpio_dir_in(PORTE.PMR.BYTE, PORTE.PDR.BYTE, 0U); /* PE0，bit2。 */
    gpio_dir_in(PORTE.PMR.BYTE, PORTE.PDR.BYTE, 1U); /* PE1，bit3。 */
    PORT0.PCR.BYTE = static_cast<unsigned char>(PORT0.PCR.BYTE | static_cast<unsigned char>((1U << 5) | (1U << 7))); /* P05、P07 上拉。 */
    PORTE.PCR.BYTE = static_cast<unsigned char>(PORTE.PCR.BYTE | static_cast<unsigned char>((1U << 0) | (1U << 1))); /* PE0、PE1 上拉，与资源表一致。 */

    /* 四路数字输出 P90–P93。 */
    gpio_dir_out(PORT9.PMR.BYTE, PORT9.PDR.BYTE, 0U); /* P90，bit0。 */
    gpio_dir_out(PORT9.PMR.BYTE, PORT9.PDR.BYTE, 1U); /* P91，bit1。 */
    gpio_dir_out(PORT9.PMR.BYTE, PORT9.PDR.BYTE, 2U); /* P92，bit2。 */
    gpio_dir_out(PORT9.PMR.BYTE, PORT9.PDR.BYTE, 3U); /* P93，bit3。 */
    PORT9.PODR.BYTE = static_cast<unsigned char>(PORT9.PODR.BYTE & static_cast<unsigned char>(~0x0FU)); /* 上电先输出低。 */

    /* P40、P41 退出外设和输出，改成模拟输入。 */
    PORT4.PMR.BYTE = static_cast<unsigned char>(PORT4.PMR.BYTE & static_cast<unsigned char>(~0x03U)); /* bit0、bit1 不再给外设。 */
    PORT4.PDR.BYTE = static_cast<unsigned char>(PORT4.PDR.BYTE & static_cast<unsigned char>(~0x03U)); /* 方向改为输入。 */
    MPC.P40PFS.BIT.ASEL = 1U; /* P40 = AN000。 */
    MPC.P41PFS.BIT.ASEL = 1U; /* P41 = AN001。 */

    MPC.PWPR.BIT.PFSWE = 0U; /* 禁止再写 PFS。 */
    MPC.PWPR.BIT.B0WI = 1U;  /* 连 PFSWE 本身也锁上。 */
}

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

/* RSPI0 主机，8 位，波特率分频 SPBR=29。 */
void spi_open(void)
{
    RSPI0.SPCR.BYTE = 0U;    /* 关闭后再改模式。 */
    RSPI0.SPPCR.BYTE = 0U;   /* 不用环回。 */
    RSPI0.SPBR = 29U;        /* 波特率分频。 */
    RSPI0.SPDCR.BYTE = 0U;   /* 单帧，不用长数据。 */
    RSPI0.SPCMD0.BIT.SPB = 7U; /* 数据长度 8 位。 */
    RSPI0.SPCR.BIT.MSTR = 1U;  /* 主机。 */
    RSPI0.SPCR.BIT.SPE = 1U;   /* 启动 SPI。 */
}

/* 交换一个字节。发送空或接收满若超时，返回值可能是旧数据。 */
uint8_t spi_xfer(uint8_t tx)
{
    uint32_t spins = 100000U;
    while ((RSPI0.SPSR.BIT.SPTEF == 0U) && (spins-- != 0U)) /* 等发送缓冲空。 */
    {
    }
    RSPI0.SPDR.LONG = tx; /* 8 位写入数据寄存器的低位。 */
    spins = 100000U;
    while ((RSPI0.SPSR.BIT.SPRF == 0U) && (spins-- != 0U)) /* 等接收缓冲满。 */
    {
    }
    return static_cast<uint8_t>(RSPI0.SPDR.LONG);
}

/* RIIC0 主机，约 100 kHz。先复位再放行。 */
void iic_open(void)
{
    RIIC0.ICCR1.BIT.ICE = 0U;    /* 内部时钟先关。 */
    RIIC0.ICCR1.BIT.IICRST = 1U; /* 进入复位。 */
    RIIC0.ICCR1.BIT.ICE = 1U;    /* 复位期间打开时钟。 */
    RIIC0.ICMR1.BIT.CKS = 3U;    /* 时钟分频档。 */
    RIIC0.ICBRH.BYTE = 36U;      /* SCL 高电平计数。 */
    RIIC0.ICBRL.BYTE = 36U;      /* SCL 低电平计数。 */
    RIIC0.ICFER.BIT.NFE = 1U;    /* 数字噪声滤波。 */
    RIIC0.ICCR1.BIT.IICRST = 0U; /* 退出复位。 */
}

/* 向 7 位地址写一个数据字节。失败时尽量发停止位。 */
bool iic_write(uint8_t addr, uint8_t data)
{
    RIIC0.ICSR2.BYTE = static_cast<unsigned char>(RIIC0.ICSR2.BYTE & static_cast<unsigned char>(~0x10U)); /* 清 NACK。 */
    RIIC0.ICCR2.BYTE = 0x62U; /* 起始条件，并按主机发送启动。 */
    if (!wait_true(RIIC0.ICSR2.BYTE, 0x80U, true, 100000U)) /* 等起始完成。 */
    {
        return false;
    }
    RIIC0.ICDRT = static_cast<unsigned char>(addr << 1); /* 地址左移，最低位 0 表示写。 */
    if (!wait_true(RIIC0.ICSR2.BYTE, 0x40U, true, 100000U)) /* 等该字节发完。 */
    {
        RIIC0.ICCR2.BIT.SP = 1U; /* 停止位，释放总线。 */
        return false;
    }
    if ((RIIC0.ICSR2.BYTE & 0x10U) != 0U) /* 从机 NACK。 */
    {
        RIIC0.ICCR2.BIT.SP = 1U;
        return false;
    }
    RIIC0.ICDRT = data; /* 数据字节。 */
    (void) wait_true(RIIC0.ICSR2.BYTE, 0x40U, true, 100000U);
    RIIC0.ICCR2.BIT.SP = 1U; /* 停止位。 */
    return true;
}

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

/* MTU3/MTU4 PWM 模式 1，计数时钟不分频。周期 60000 个 PCLKB，即 1 kHz。 */
void pwm_open(void)
{
    MTU.TSTRA.BIT.CST3 = 0U; /* 停 MTU3。 */
    MTU.TSTRA.BIT.CST4 = 0U; /* 停 MTU4。 */
    MTU3.TCR.BIT.CCLR = 1U;  /* TGRA 比较匹配时清零，TGRA 就是周期。 */
    MTU3.TCR.BIT.TPSC = 0U;  /* 不分频，60 MHz。 */
    MTU3.TMDR1.BIT.MD = 2U;  /* PWM 模式 1。 */
    MTU3.TIORH.BIT.IOA = 1U; /* MTIOC3A：初始输出低，匹配后变高。 */
    MTU3.TIORH.BIT.IOB = 2U; /* MTIOC3B：匹配后变低。 */
    MTU3.TGRA = 59999U;      /* 周期 = 60000 / 60 MHz = 1 ms。 */
    MTU3.TGRB = 30000U;      /* 约 50% 占空比，对应 P17。 */
    MTU3.TCNT = 0U;

    MTU4.TCR.BIT.CCLR = 1U;
    MTU4.TCR.BIT.TPSC = 0U;
    MTU4.TMDR1.BIT.MD = 2U;
    MTU4.TIORH.BIT.IOA = 1U; /* MTIOC4A = P24。 */
    MTU4.TIORL.BIT.IOC = 1U; /* MTIOC4C = P25。 */
    MTU4.TGRA = 59999U;      /* 同样 1 kHz。 */
    MTU4.TGRB = 15000U;      /* 约 25%。 */
    MTU4.TGRC = 7500U;       /* 约 12.5%，P25。 */
    MTU4.TCNT = 0U;

    MTU4.TIORL.BIT.IOD = 1U; /* 再打开 MTIOC4D，给第 4 路占空比。 */
    MTU4.TGRD = 3750U;       /* 约 6.25%。 */
    MTU.TOERA.BIT.OE3B = 1U; /* 允许 MTIOC3B 输出到引脚。 */
    MTU.TOERA.BIT.OE4A = 1U; /* MTIOC4A。 */
    MTU.TOERA.BIT.OE4C = 1U; /* MTIOC4C。 */
    MTU.TOERA.BIT.OE4D = 1U; /* MTIOC4D。 */
    MTU.TSTRA.BIT.CST3 = 1U; /* 启动 MTU3。 */
    MTU.TSTRA.BIT.CST4 = 1U; /* 启动 MTU4。 */
}

/* 单次扫描 AN000 和 AN001，等转换结束再读 12 位结果。 */
void adc_sample(void)
{
    S12AD.ADANSA0.WORD = 0x0003U; /* bit0、bit1：AN000、AN001。 */
    S12AD.ADCSR.BIT.ADCS = 0U;    /* 单次扫描，不是连续。 */
    S12AD.ADCSR.BIT.ADST = 1U;    /* 开始转换。 */
    uint32_t spins = 200000U;
    while ((S12AD.ADCSR.BIT.ADST != 0U) && (spins-- != 0U)) /* ADST 变 0 表示转完。 */
    {
    }
    g_adc0 = static_cast<uint16_t>(S12AD.ADDR0 & 0x0FFFU);
    g_adc1 = static_cast<uint16_t>(S12AD.ADDR1 & 0x0FFFU);
}

/* 写 12 位 DAC，右对齐，并打开通道 0 模拟输出。 */
void dac_write(uint16_t code)
{
    g_dac = static_cast<uint16_t>(code & 0x0FFFU); /* 只留 12 位，供状态上报。 */
    DA.DADPR.BIT.DPSEL = 1U; /* 数据右对齐。 */
    DA.DADR0 = g_dac;
    DA.DACR.BIT.DAOE0 = 1U;  /* DA0 输出使能。 */
}

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

/* 以太网：DMA 软件复位，写一个本地 MAC，全双工并打开收发。这里不组帧。 */
void eth_open(void)
{
    EDMAC0.EDMR.BIT.SWR = 1U; /* 软件复位。 */
    uint32_t spins = 200000U;
    while ((EDMAC0.EDMR.BIT.SWR != 0U) && (spins-- != 0U)) /* 复位位会自己清掉。 */
    {
    }
    ETHERC0.MAHR = 0x02000000UL;       /* MAC 高 32 位：02:00:00:00。 */
    ETHERC0.MALR.LONG = 0x00000001UL;  /* MAC 低 16 位：00:01。 */
    ETHERC0.ECMR.BIT.DM = 1U;          /* 全双工。 */
    ETHERC0.ECMR.BIT.RE = 1U;          /* 接收使能。 */
    ETHERC0.ECMR.BIT.TE = 1U;          /* 发送使能。 */
}

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

/* 把 16 位数写成 4 个大写十六进制字符。 */
void hex4(char * dst, uint16_t value)
{
    static const char kHex[] = "0123456789ABCDEF";
    dst[0] = kHex[(value >> 12) & 0x0FU];
    dst[1] = kHex[(value >> 8) & 0x0FU];
    dst[2] = kHex[(value >> 4) & 0x0FU];
    dst[3] = kHex[value & 0x0FU];
}

/* 把 8 位数写成 2 个十六进制字符。 */
void hex2(char * dst, uint8_t value)
{
    static const char kHex[] = "0123456789ABCDEF";
    dst[0] = kHex[value >> 4];
    dst[1] = kHex[value & 0x0FU];
}

/* 日志口一行：tick、两路 ADC、输入掩码。不走业务口。 */
void log_line(void)
{
    char line[] = "RX t=0000 adc=0000/0000 in=00\r\n"; /* 占位，下面按固定列覆写。 */
    hex4(&line[5], static_cast<uint16_t>(g_tick)); /* "t=" 后面 4 位。 */
    hex4(&line[14], g_adc0);                       /* AN000。 */
    hex4(&line[19], g_adc1);                       /* AN001。 */
    hex2(&line[27], g_inputs);                     /* "in=" 后面 2 位。 */
    sci_write(kLogUart, line);
}

/* 千分比换成 MTU 比较值。周期固定 60000，100% 时停在 59999，避免等于清零点。 */
uint16_t duty_count(uint16_t duty_permille)
{
    const uint32_t period = 60000U;
    uint32_t cmp = (period * duty_permille) / 1000U;
    if (cmp >= period)
    {
        cmp = period - 1U;
    }
    return static_cast<uint16_t>(cmp);
}

/* 改四路 PWM 比较值。0=MTU3.TGRB，1=MTU4.TGRB，2=MTU4.TGRC，3=MTU4.TGRD。 */
int apply_pwm(uint8_t channel, uint16_t duty_permille)
{
    const uint16_t cmp = duty_count(duty_permille);
    switch (channel)
    {
    case 0U:
        MTU3.TGRB = cmp; /* P17 / MTIOC3B。 */
        break;
    case 1U:
        MTU4.TGRB = cmp; /* MTIOC4B。 */
        break;
    case 2U:
        MTU4.TGRC = cmp; /* P25 / MTIOC4C。 */
        break;
    case 3U:
        MTU4.TGRD = cmp; /* MTIOC4D。 */
        break;
    default:
        return -1; /* 只有 0–3。 */
    }
    return 0;
}

/* 协议回调：把一帧字节写到 SCI2。按长度发，0x00 也要出去。 */
extern "C" void rx_write(void * ctx, const uint8_t * data, uint16_t len)
{
    uint16_t i;
    (void) ctx; /* 串口是固定的 SCI2，不用上下文。 */
    for (i = 0U; i < len; ++i)
    {
        sci_putc(kAppUart, data[i]);
    }
}

/* 上位机设置四路 GPIO 输出。 */
extern "C" void rx_outputs(void * ctx, uint8_t mask)
{
    (void) ctx;
    gpio_write(mask);
}

/* 上位机设置 DAC。 */
extern "C" void rx_dac(void * ctx, uint16_t code)
{
    (void) ctx;
    dac_write(code);
}

/* 上位机设置 PWM。通道或千分比越界则失败。 */
extern "C" int rx_pwm(void * ctx, uint8_t channel, uint16_t duty)
{
    (void) ctx;
    if ((channel > 3U) || (duty > 1000U))
    {
        return -1;
    }
    return apply_pwm(channel, duty);
}

/* 上位机发经典 CAN。通道 0/1 是 CAN0/CAN1。fd 参数忽略，本芯片不支持 CAN FD。 */
extern "C" int rx_can(void * ctx, uint8_t channel, uint32_t id, const uint8_t * data, uint8_t dlc, int fd)
{
    (void) ctx;
    (void) fd; /* 协议层在调用前已经拒绝 CAN FD。 */
    volatile struct st_can * can = (channel == 0U) ? &CAN0 : &CAN1;
    return can_send(*can, static_cast<uint16_t>(id), data, dlc) ? 0 : -1;
}

/* 填写给 PC 的状态。can_fd 保持 0。 */
extern "C" void rx_fill(void * ctx, HlEnvelope * msg)
{
    const char * name = "RX71M";
    unsigned i = 0U;
    (void) ctx;
    msg->tick = g_tick;
    msg->adc0 = g_adc0;
    msg->adc1 = g_adc1;
    msg->inputs = g_inputs;
    msg->outputs = g_outputs;
    msg->dac = g_dac;
    msg->board = HL_BOARD_RX71M; /* 协议板卡号 1。 */
    msg->can_fd = 0;
    for (; (name[i] != '\0') && (i + 1U < HL_NAME_MAX); ++i)
    {
        msg->name[i] = name[i]; /* 拷进定长名字，留一个结尾 0。 */
    }
    msg->name[i] = '\0';
}

/* 登记回调。第三个参数 0：帧标志不声明 CAN FD。 */
void link_init(void)
{
    HlBoardOps ops;
    std::memset(&ops, 0, sizeof ops); /* 未用的回调保持空。 */
    ops.write = rx_write;
    ops.set_outputs = rx_outputs;
    ops.set_dac = rx_dac;
    ops.set_pwm = rx_pwm;
    ops.send_can = rx_can;
    ops.fill_snapshot = rx_fill;
    hl_board_init(&g_link, HL_BOARD_RX71M, 0, &ops);
}

/* 把 SCI2 里已经收到的字节喂给协议解析。没有数据就立刻返回。 */
void poll_link(void)
{
    int rx;
    while ((rx = sci_getc(kAppUart)) >= 0)
    {
        hl_board_push(&g_link, static_cast<uint8_t>(rx));
    }
}

/* 上电顺序：先给时钟，再配引脚，然后各外设，最后才允许协议回包。 */
void bringup(void)
{
    modules_start();
    pins_apply();
    sci_open(kLogUart, kUartBaud, 0U); /* CKS=0，115200。 */
    sci_open(kAppUart, kUartBaud, 0U);
    sci_open(kLinUart, kLinBaud, 1U);  /* 低波特率用 CKS=1，再除 4。 */
    sci_write(kLogUart, "RX71M resources up\r\n");
    spi_open();
    iic_open();
    can_open(CAN0);
    can_open(CAN1);
    pwm_open();
    dac_write(0U); /* 上电 DAC 输出 0。 */
    usb_open();
    eth_open();
    link_init();
}

/* 500 ms 周期：采样、演示总线。已连接后不再改 GPIO 和 DAC。 */
void poll(void)
{
    const uint8_t sample = static_cast<uint8_t>(g_tick); /* 用本拍计数当演示数据。 */
    ++g_tick;
    if (!g_link.linked)
    {
        /* 还没连上 PC 时，输出和 DAC 跟着计数走，方便示波器看。 */
        gpio_write(static_cast<uint8_t>(g_tick & 0x0FU));
        dac_write(static_cast<uint16_t>((g_tick << 4) & 0x0FFFU));
    }
    /* 已连接后保留上位机写过的 GPIO、DAC、PWM。 */
    g_inputs = gpio_read();
    adc_sample();
    (void) spi_xfer(sample);         /* 演示 SPI 交换，不看返回值。 */
    (void) iic_write(0x50U, sample); /* 向地址 0x50 写一个字节，总线上没有器件也无妨。 */
    if (can_send(CAN0, 0x123U, &sample, 1U))
    {
        hl_board_send_can_log(&g_link, 0U, 0x123U, &sample, 1U, 0); /* 未连接时函数内部直接返回。 */
    }
    if (can_send(CAN1, 0x321U, &sample, 1U))
    {
        hl_board_send_can_log(&g_link, 1U, 0x321U, &sample, 1U, 0);
    }
    lin_break_and_sync();
    hl_board_send_snapshot(&g_link); /* 已连接才真正发 Snapshot。 */
    log_line();                      /* 文本只走 SCI1。 */
}
} // namespace

/* 线程主体。每 20 ms 收一次串口；每 25 拍（约 500 ms）做一次外设轮询。 */
extern "C" void board_app_run(void)
{
    uint32_t phase = 0U;
    bringup();
    for (;;)
    {
        poll_link();                   /* 命令尽量在 20 ms 内被处理。 */
        if ((phase % 25U) == 0U)
        {
            poll();                    /* 采样和周期上报。 */
        }
        ++phase;
        vTaskDelay(pdMS_TO_TICKS(20)); /* 把 CPU 让给空闲任务。 */
    }
}
