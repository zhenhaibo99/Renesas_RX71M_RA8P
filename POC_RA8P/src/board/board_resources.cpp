#include "board_resources.hpp"

#include <cstring>

/* HL1 帧和 Protobuf 编解码。编译进本文件，不单独进工程文件列表。 */
#include "../../../protocol/host_link.c"

#include "bsp_api.h"   /* 引脚、延时、寄存器保护。 */
#include "r_ioport.h"  /* IOPORT 配置宏。 */
#include "FreeRTOS.h"
#include "task.h"      /* vTaskDelay。 */

/* 文件内部符号，避免和 FSP 全局名字冲突。 */
namespace
{
/* PCLKA：SCI、SPI、IIC 的时钟，125 MHz。 */
constexpr uint32_t kPclkaHz = 125000000UL;
/* PCLKD：GPT 的计数时钟，250 MHz。 */
constexpr uint32_t kPclkdHz = 250000000UL;
/* 日志口和业务口都是 115200。 */
constexpr uint32_t kUartBaud = 115200UL;
/* LIN 用较低波特率，便于用 GPIO 拉出 Break。 */
constexpr uint32_t kLinBaud = 19200UL;
/* 四路 PWM 的目标频率。 */
constexpr uint32_t kPwmHz = 1000UL;

/* PFS 配成外设功能，而不是普通 GPIO。 */
constexpr uint32_t kPeriph = IOPORT_CFG_PERIPHERAL_PIN;
/* SCI0/2/4/6/8 共用这一组复用号。 */
constexpr uint32_t kSciEven = IOPORT_PERIPHERAL_SCI0_2_4_6_8;
/* SCI1/3/5/7/9 共用这一组复用号。 */
constexpr uint32_t kSciOdd = IOPORT_PERIPHERAL_SCI1_3_5_7_9;

/* 日志文本口：SCI8。 */
R_SCI_B0_Type * const kLogUart = R_SCI_B8;
/* 与 PC 通信的业务口：SCI7，走 HL1。 */
R_SCI_B0_Type * const kAppUart = R_SCI_B7;
/* LIN：SCI2。 */
R_SCI_B0_Type * const kLinUart = R_SCI_B2;

/* 约每 500 ms 加 1，同时当作演示数据。 */
uint32_t g_tick = 0U;
/* AN001 最近一次采样。 */
uint16_t g_adc0 = 0U;
/* AN007 最近一次采样。 */
uint16_t g_adc1 = 0U;
/* 四路输入，bit0–bit3。 */
uint8_t g_inputs = 0U;
/* 四路输出的当前掩码。连上 PC 后由上位机接管。 */
uint8_t g_outputs = 0U;
/* DAC 当前 12 位码。 */
uint16_t g_dac = 0U;
/* GPT 周期寄存器值，供上位机改占空比时换算。 */
uint32_t g_pwm_period = 0U;
/* PC 链路。未收到合法帧之前 linked 为 0，不往业务口发二进制。 */
HlBoardLink g_link;

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

/* 按资源表配置全部引脚。必须先打开 PFS 写保护。 */
void pins_apply(void)
{
    R_BSP_PinAccessEnable(); /* 允许写 PFS。结束前必须关掉。 */

    /* UART：TX 中等驱动，RX 上拉。SCI8 用偶数复用，SCI7 用奇数复用，SCI2 再用偶数复用。 */
    const uint32_t uart_tx = kPeriph | IOPORT_CFG_DRIVE_MID;
    const uint32_t uart_rx = kPeriph | IOPORT_CFG_PULLUP_ENABLE;
    R_BSP_PinCfg(BSP_IO_PORT_13_PIN_02, uart_tx | kSciEven); /* SCI8 TX = PD02，日志。 */
    R_BSP_PinCfg(BSP_IO_PORT_13_PIN_03, uart_rx | kSciEven); /* SCI8 RX = PD03。 */
    R_BSP_PinCfg(BSP_IO_PORT_08_PIN_09, uart_tx | kSciOdd);  /* SCI7 TX = P809，业务。 */
    R_BSP_PinCfg(BSP_IO_PORT_08_PIN_08, uart_rx | kSciOdd);  /* SCI7 RX = P808。 */
    R_BSP_PinCfg(BSP_IO_PORT_08_PIN_01, uart_tx | kSciEven); /* SCI2 TX = P801，LIN。 */
    R_BSP_PinCfg(BSP_IO_PORT_08_PIN_02, uart_rx | kSciEven); /* SCI2 RX = P802。 */

    /* CAN FD 发送脚中等驱动，接收脚上拉。 */
    const uint32_t can = kPeriph | IOPORT_PERIPHERAL_CAN | IOPORT_CFG_DRIVE_MID;
    R_BSP_PinCfg(BSP_IO_PORT_03_PIN_13, can);                        /* CANFD0 CTX = P313。 */
    R_BSP_PinCfg(BSP_IO_PORT_03_PIN_12, can | IOPORT_CFG_PULLUP_ENABLE); /* CANFD0 CRX = P312。 */
    R_BSP_PinCfg(BSP_IO_PORT_06_PIN_09, can);                        /* CANFD1 CTX = P609。 */
    R_BSP_PinCfg(BSP_IO_PORT_06_PIN_10, can | IOPORT_CFG_PULLUP_ENABLE); /* CANFD1 CRX = P610。 */

    /* SPI1 主机四线。 */
    const uint32_t spi = kPeriph | IOPORT_PERIPHERAL_SPI | IOPORT_CFG_DRIVE_MID;
    R_BSP_PinCfg(BSP_IO_PORT_01_PIN_00, spi); /* MISO = P100。 */
    R_BSP_PinCfg(BSP_IO_PORT_01_PIN_01, spi); /* MOSI = P101。 */
    R_BSP_PinCfg(BSP_IO_PORT_01_PIN_02, spi); /* RSPCK = P102。 */
    R_BSP_PinCfg(BSP_IO_PORT_01_PIN_03, spi); /* SSL = P103。 */

    /* IIC0 开漏并上拉。 */
    const uint32_t iic = kPeriph | IOPORT_PERIPHERAL_IIC | IOPORT_CFG_NMOS_ENABLE | IOPORT_CFG_PULLUP_ENABLE;
    R_BSP_PinCfg(BSP_IO_PORT_04_PIN_00, iic); /* SCL = P400。 */
    R_BSP_PinCfg(BSP_IO_PORT_04_PIN_01, iic); /* SDA = P401。 */

    /* GPT 输出，四路 PWM。 */
    const uint32_t gpt = kPeriph | IOPORT_PERIPHERAL_GPT1 | IOPORT_CFG_DRIVE_MID;
    R_BSP_PinCfg(BSP_IO_PORT_01_PIN_05, gpt); /* GPT1A = P105。 */
    R_BSP_PinCfg(BSP_IO_PORT_01_PIN_04, gpt); /* GPT1B = P104。 */
    R_BSP_PinCfg(BSP_IO_PORT_05_PIN_01, gpt); /* GPT12A = P501。 */
    R_BSP_PinCfg(BSP_IO_PORT_08_PIN_10, gpt); /* GPT10A = P810。 */

    /* 四路数字输入，内部上拉。 */
    const uint32_t din = IOPORT_CFG_PULLUP_ENABLE;
    R_BSP_PinCfg(BSP_IO_PORT_00_PIN_08, din); /* P008，对应掩码 bit0。 */
    R_BSP_PinCfg(BSP_IO_PORT_00_PIN_09, din); /* P009，bit1。 */
    R_BSP_PinCfg(BSP_IO_PORT_00_PIN_12, din); /* P012，bit2。 */
    R_BSP_PinCfg(BSP_IO_PORT_04_PIN_10, din); /* P410，bit3。 */

    /* 四路数字输出，上电为低。 */
    const uint32_t dout = IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_LOW;
    R_BSP_PinCfg(BSP_IO_PORT_03_PIN_03, dout); /* P303，bit0。 */
    R_BSP_PinCfg(BSP_IO_PORT_06_PIN_00, dout); /* P600，bit1。 */
    R_BSP_PinCfg(BSP_IO_PORT_10_PIN_07, dout); /* PA07，bit2。 */
    R_BSP_PinCfg(BSP_IO_PORT_04_PIN_09, dout); /* P409，bit3。 */

    /* 模拟脚：两路 AD 和一路 DA。 */
    R_BSP_PinCfg(BSP_IO_PORT_00_PIN_01, IOPORT_CFG_ANALOG_ENABLE); /* P001 = AN001。 */
    R_BSP_PinCfg(BSP_IO_PORT_00_PIN_07, IOPORT_CFG_ANALOG_ENABLE); /* P007 = AN007。 */
    R_BSP_PinCfg(BSP_IO_PORT_00_PIN_14, IOPORT_CFG_ANALOG_ENABLE); /* P014 = DA0。 */

    /* USB FS 设备：DP/DM/VBUS 走 USB 复用，P500 拉低选择设备角色相关电平。 */
    const uint32_t usb = kPeriph | IOPORT_PERIPHERAL_USB_FS | IOPORT_CFG_DRIVE_HIGH;
    R_BSP_PinCfg(BSP_IO_PORT_08_PIN_14, usb);  /* USB_DP = P814。 */
    R_BSP_PinCfg(BSP_IO_PORT_08_PIN_15, usb);  /* USB_DM = P815。 */
    R_BSP_PinCfg(BSP_IO_PORT_04_PIN_07, usb);  /* VBUS = P407。 */
    R_BSP_PinCfg(BSP_IO_PORT_05_PIN_00, dout); /* P500 输出低。 */

    /* RGMII 数据与时钟，高驱动。 */
    const uint32_t rgmii = kPeriph | IOPORT_PERIPHERAL_ETHER_RGMII | IOPORT_CFG_DRIVE_HIGH;
    R_BSP_PinCfg(BSP_IO_PORT_03_PIN_04, rgmii); /* TXD3 = P304。 */
    R_BSP_PinCfg(BSP_IO_PORT_03_PIN_05, rgmii); /* TXD2 = P305。 */
    R_BSP_PinCfg(BSP_IO_PORT_03_PIN_06, rgmii); /* TXD1 = P306。 */
    R_BSP_PinCfg(BSP_IO_PORT_03_PIN_07, rgmii); /* TXD0 = P307。 */
    R_BSP_PinCfg(BSP_IO_PORT_03_PIN_10, rgmii); /* TX_CTL = P310。 */
    R_BSP_PinCfg(BSP_IO_PORT_03_PIN_09, rgmii); /* TX_CLK = P309。 */
    R_BSP_PinCfg(BSP_IO_PORT_09_PIN_06, rgmii); /* RXD0 = P906。 */
    R_BSP_PinCfg(BSP_IO_PORT_09_PIN_07, rgmii); /* RXD1 = P907。 */
    R_BSP_PinCfg(BSP_IO_PORT_09_PIN_08, rgmii); /* RXD2 = P908。 */
    R_BSP_PinCfg(BSP_IO_PORT_09_PIN_09, rgmii); /* RXD3 = P909。 */
    R_BSP_PinCfg(BSP_IO_PORT_02_PIN_06, rgmii); /* RX_CTL = P206。 */
    R_BSP_PinCfg(BSP_IO_PORT_09_PIN_05, rgmii); /* RX_CLK = P905。 */
    /* MDIO 管理口。MDC 推挽，MDIO 上拉。 */
    const uint32_t mdio = kPeriph | IOPORT_PERIPHERAL_ETHER_MII | IOPORT_CFG_DRIVE_MID;
    R_BSP_PinCfg(BSP_IO_PORT_04_PIN_15, mdio);                         /* MDC = P415。 */
    R_BSP_PinCfg(BSP_IO_PORT_04_PIN_14, mdio | IOPORT_CFG_PULLUP_ENABLE); /* MDIO = P414。 */
    R_BSP_PinCfg(BSP_IO_PORT_07_PIN_08, IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_HIGH); /* PHY 复位=P708，先保持释放。 */

    R_BSP_PinAccessDisable(); /* 重新锁上 PFS，避免后面误改引脚。 */
}

/* 把本文件要用的外设从模块停止里放出来。写 0 表示退出停止。 */
void modules_start(void)
{
    R_BSP_RegisterProtectDisable(BSP_REG_PROTECT_OM_LPC_BATT); /* 允许改 MSTP。 */
    /* MSTPCRB：SCI、CANFD、SPI、IIC、USB 等。位号与手册的模块停止位一致。 */
    R_MSTP->MSTPCRB &= ~((1UL << (31U - 8U)) | (1UL << (31U - 7U)) | (1UL << (31U - 2U)) |
                         (1UL << (19U - 1U)) | (1UL << 9U) | (1UL << 11U) | (1UL << 15U) | (1UL << 14U));
    R_MSTP->MSTPCRC &= ~((1UL << 27U) | (1UL << 26U)); /* MSTPCRC：以太网相关。 */
    R_MSTP->MSTPCRD &= ~((1UL << 21U) | (1UL << 20U)); /* MSTPCRD：ADC_B、DAC_B。 */
    /* MSTPCRE：GPT1、GPT12、GPT10。31-n 是手册位号 n 在寄存器里的位置。 */
    R_MSTP->MSTPCRE &= ~((1UL << (31U - 1U)) | (1UL << (31U - 10U)) | (1UL << (31U - 12U)));
    (void) R_MSTP->MSTPCRB; /* 读回一次，让停止位的写真正完成。 */
    R_BSP_RegisterProtectEnable(BSP_REG_PROTECT_OM_LPC_BATT); /* 重新保护。 */
    R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MILLISECONDS);    /* 给刚解除停止的模块一个时钟周期以上。 */
}

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

/* SPI1 主机，8 位，波特率分频 SPBR=61。 */
void spi_open(void)
{
    R_SPI1->SPCR = 0U;   /* 关闭后再改模式。 */
    R_SPI1->SPPCR = 0U;  /* 不用环回等测试模式。 */
    R_SPI1->SPBR = 61U;  /* 波特率分频。 */
    R_SPI1->SPSCR = 0U;  /* 只用命令寄存器 0。 */
    R_SPI1->SPCMD[0] = static_cast<uint16_t>(7U << 8); /* 数据长 8 位。 */
    R_SPI1->SPCR = static_cast<uint8_t>((1U << 3) | (1U << 6)); /* 主机，并启动 SPI。 */
}

/* 交换一个字节。发送空或接收满超时则返回 0。 */
uint8_t spi_xfer(uint8_t tx)
{
    if (!wait_u8(R_SPI1->SPSR, static_cast<uint8_t>(1U << 5), true, 100000U)) /* 发送缓冲空。 */
    {
        return 0U;
    }
    R_SPI1->SPDR_BY = tx;
    if (!wait_u8(R_SPI1->SPSR, static_cast<uint8_t>(1U << 7), true, 100000U)) /* 接收缓冲满。 */
    {
        return 0U;
    }
    return R_SPI1->SPDR_BY;
}

/* IIC0 主机，约 100 kHz。先复位模块再放行。 */
void iic_open(void)
{
    R_IIC0->ICCR1_b.ICE = 0U;     /* 内部时钟先关。 */
    R_IIC0->ICCR1_b.IICRST = 1U;  /* 进入复位。 */
    R_IIC0->ICCR1_b.ICE = 1U;     /* 复位期间打开时钟。 */
    R_IIC0->ICMR1_b.CKS = 3U;     /* 时钟分频档。 */
    R_IIC0->ICBRH = 38U;          /* SCL 高电平计数。 */
    R_IIC0->ICBRL = 38U;          /* SCL 低电平计数。 */
    R_IIC0->ICMR3 = 0x01U;        /* 噪声滤波等基本设置。 */
    R_IIC0->ICFER = 0x30U;        /* 打开主机需要的功能位。 */
    R_IIC0->ICCR1_b.IICRST = 0U;  /* 退出复位，开始工作。 */
}

/* 向 7 位地址写一个数据字节。失败时尽量发出停止位。 */
bool iic_write(uint8_t addr, uint8_t data)
{
    R_IIC0->ICSR2 = static_cast<uint8_t>(R_IIC0->ICSR2 & static_cast<uint8_t>(~0x10U)); /* 清 NACK。 */
    R_IIC0->ICCR2 = 0x62U; /* 起始条件，并按主机发送启动。 */
    if (!wait_u8(R_IIC0->ICSR2, 0x80U, true, 100000U)) /* 等起始条件完成。 */
    {
        return false;
    }
    R_IIC0->ICDRT = static_cast<uint8_t>(addr << 1); /* 地址左移，最低位 0 表示写。 */
    if (!wait_u8(R_IIC0->ICSR2, 0x40U, true, 100000U)) /* 等发送结束。 */
    {
        R_IIC0->ICCR2_b.SP = 1U; /* 发停止位，释放总线。 */
        return false;
    }
    if ((R_IIC0->ICSR2 & 0x10U) != 0U) /* 从机 NACK。 */
    {
        R_IIC0->ICCR2_b.SP = 1U;
        return false;
    }
    R_IIC0->ICDRT = data; /* 数据字节。 */
    (void) wait_u8(R_IIC0->ICSR2, 0x40U, true, 100000U);
    R_IIC0->ICCR2_b.SP = 1U; /* 停止位。 */
    return true;
}

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

/* 启动一路 GPT。both 为真时 A/B 两相都输出，B 相占空比是 A 的一半。 */
void gpt_start(R_GPT0_Type * gpt, uint32_t channel, uint32_t duty, bool both)
{
    gpt->GTWP = 0xA500U;            /* 写保护解锁键。 */
    R_GPT0->GTSTP = 1UL << channel; /* 先停这个通道。GTSTP 在公共寄存器。 */
    gpt->GTCR = 0U;                 /* 锯齿波向上计数。 */
    gpt->GTPR = (kPclkdHz / kPwmHz) - 1U; /* 1 kHz 的周期。 */
    gpt->GTCCR[0] = duty;           /* A 相比较值。 */
    uint32_t ior = 6UL | (1UL << 8); /* A 相输出模式，并打开 A 相输出。 */
    if (both)
    {
        gpt->GTCCR[1] = duty / 2U;          /* B 相用一半占空比。 */
        ior |= (6UL << 16) | (1UL << 24);   /* 同样打开 B 相。 */
    }
    gpt->GTIOR = ior;
    gpt->GTWP = 0xA500U;            /* 再次写入键，保持后面还能改比较值。 */
    R_GPT0->GTSTR = 1UL << channel; /* 启动计数。 */
}

/* ADC_B：打开时钟，扫描组 0 选 AN001 和 AN007。 */
void adc_open(void)
{
    R_ADC_B->ADCLKENR_b.CLKEN = 1U; /* 打开 ADC 时钟。 */
    (void) wait_flag(R_ADC_B->ADCLKSR, 1UL, true, 200000U); /* 等时钟稳定。 */
    R_ADC_B->ADCLKCR = (2UL << 0) | (2UL << 16);            /* A/D 转换时钟分频。 */
    R_ADC_B->ADSSTR0 = (0x20UL << 0) | (0x20UL << 16);      /* 两路采样时间。 */
    R_ADC_B->ADCHCR0 = (1UL << 8);  /* 扫描组 0 的第一路：AN001。 */
    R_ADC_B->ADCHCR1 = (7UL << 8);  /* 第二路：AN007。 */
}

/* 启动一次扫描组 0，等 1 ms 后读结果。 */
void adc_sample(void)
{
    R_ADC_B->ADSTR[0] = 1U; /* 启动扫描组 0。 */
    R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MILLISECONDS);
    g_adc0 = static_cast<uint16_t>(R_ADC_B->ADDR[0] & 0xFFFFU); /* AN001。 */
    g_adc1 = static_cast<uint16_t>(R_ADC_B->ADDR[1] & 0xFFFFU); /* AN007。 */
}

/* DAC_B0：右对齐，允许输出并打开转换。 */
void dac_open(void)
{
    R_DAC_B0->DACR1_b.DPSEL = 1U;   /* 数据右对齐。 */
    R_DAC_B0->DADR = 0U;            /* 上电输出 0。 */
    R_DAC_B0->DACR0_b.DAOUTDIS = 0U; /* 不禁止模拟输出。 */
    R_DAC_B0->DACR0_b.DACEN = 1U;   /* 使能 DAC。 */
}

/* 写入 12 位码，并记住给上位机状态用。 */
void dac_write(uint16_t code)
{
    g_dac = static_cast<uint16_t>(code & 0x0FFFU); /* 只留 12 位。 */
    R_DAC_B0->DADR = g_dac;
}

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

/* 以太网 DMA 软件复位，等复位位自己清掉。这里只做模块活着，不发帧。 */
void eth_open(void)
{
    R_ETHERC_EDMAC->EDMR_b.SWR = 1U;
    (void) wait_flag(R_ETHERC_EDMAC->EDMR, 1UL, false, 200000U);
}

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

/* 把 16 位数写成 4 个大写十六进制字符，给日志口用。 */
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
    char line[] = "RA t=0000 adc=0000/0000 in=00\r\n"; /* 占位，下面按固定列覆写。 */
    hex4(&line[5], static_cast<uint16_t>(g_tick)); /* "t=" 后面 4 位。 */
    hex4(&line[14], g_adc0);                       /* 第一路 ADC。 */
    hex4(&line[19], g_adc1);                       /* 第二路 ADC。 */
    hex2(&line[27], g_inputs);                     /* "in=" 后面 2 位。 */
    sci_write(kLogUart, line);
}

/* 按千分比改某一路 PWM。0–3 对应 GPT1A、GPT1B、GPT12A、GPT10A。 */
int apply_pwm(uint8_t channel, uint16_t duty_permille)
{
    uint32_t cmp;
    if ((g_pwm_period == 0U) || (channel > 3U) || (duty_permille > 1000U))
    {
        return -1; /* 周期还没建好，或参数越界。 */
    }
    cmp = (g_pwm_period * duty_permille) / 1000U; /* 千分比换成比较值。 */
    if (cmp > g_pwm_period)
    {
        cmp = g_pwm_period; /* 100% 时不超过周期寄存器。 */
    }
    switch (channel)
    {
    case 0U:
        R_GPT1->GTWP = 0xA500U;    /* 解锁 GPT1。 */
        R_GPT1->GTCCR[0] = cmp;    /* A 相。 */
        break;
    case 1U:
        R_GPT1->GTWP = 0xA500U;
        R_GPT1->GTCCR[1] = cmp;    /* B 相。 */
        break;
    case 2U:
        R_GPT12->GTWP = 0xA500U;
        R_GPT12->GTCCR[0] = cmp;
        break;
    default:
        R_GPT10->GTWP = 0xA500U;
        R_GPT10->GTCCR[0] = cmp;   /* 通道 3。 */
        break;
    }
    return 0;
}

/* 协议栈回调：把一帧字节写到业务串口。C 链接，供 host_link.c 调用。 */
extern "C" void ra_write(void * ctx, const uint8_t * data, uint16_t len)
{
    uint16_t i;
    (void) ctx; /* 回调上下文没用，串口是固定的 SCI7。 */
    for (i = 0U; i < len; ++i)
    {
        sci_putc(kAppUart, data[i]); /* 按长度发送，0x00 也要发出去。 */
    }
}

/* 上位机设置 GPIO 输出。 */
extern "C" void ra_outputs(void * ctx, uint8_t mask)
{
    (void) ctx;
    gpio_write(mask);
}

/* 上位机设置 DAC。 */
extern "C" void ra_dac(void * ctx, uint16_t code)
{
    (void) ctx;
    dac_write(code);
}

/* 上位机设置 PWM 占空比。 */
extern "C" int ra_pwm(void * ctx, uint8_t channel, uint16_t duty)
{
    (void) ctx;
    return apply_pwm(channel, duty);
}

/* 上位机发 CAN。通道 0/1 分别是 CANFD0/CANFD1。fd 非 0 时发 CAN FD。 */
extern "C" int ra_can(void * ctx, uint8_t channel, uint32_t id, const uint8_t * data, uint8_t dlc, int fd)
{
    R_CANFD_Type * can = (channel == 0U) ? R_CANFD0 : R_CANFD1;
    (void) ctx;
    return canfd_send(can, id, data, dlc, fd != 0) ? 0 : -1; /* 0 成功，-1 失败。 */
}

/* 填写给 PC 的状态。can_fd=1，因为本板支持 CAN FD。 */
extern "C" void ra_fill(void * ctx, HlEnvelope * msg)
{
    const char * name = "RA8P1";
    unsigned i = 0U;
    (void) ctx;
    msg->tick = g_tick;
    msg->adc0 = g_adc0;
    msg->adc1 = g_adc1;
    msg->inputs = g_inputs;
    msg->outputs = g_outputs;
    msg->dac = g_dac;
    msg->board = HL_BOARD_RA8P; /* 协议板卡号 2。 */
    msg->can_fd = 1;
    for (; (name[i] != '\0') && (i + 1U < HL_NAME_MAX); ++i)
    {
        msg->name[i] = name[i]; /* 拷进定长名字，留一个结尾 0。 */
    }
    msg->name[i] = '\0';
}

/* 登记回调。板卡号 RA8P，并声明支持 CAN FD。 */
void link_init(void)
{
    HlBoardOps ops;
    std::memset(&ops, 0, sizeof ops); /* 未用的回调保持空。 */
    ops.write = ra_write;
    ops.set_outputs = ra_outputs;
    ops.set_dac = ra_dac;
    ops.set_pwm = ra_pwm;
    ops.send_can = ra_can;
    ops.fill_snapshot = ra_fill;
    hl_board_init(&g_link, HL_BOARD_RA8P, 1, &ops); /* 第三个参数 1：帧标志带 CAN FD。 */
}

/* 把业务口里已经收到的字节喂给协议解析。无数据就立刻返回。 */
void poll_link(void)
{
    int rx;
    while ((rx = sci_getc(kAppUart)) >= 0)
    {
        hl_board_push(&g_link, static_cast<uint8_t>(rx));
    }
}

/* 上电顺序：引脚、时钟门控，然后各外设，最后才允许协议回包。 */
void bringup(void)
{
    pins_apply();
    modules_start();
    sci_open(kLogUart, kUartBaud, 0U); /* CKS=0，115200。 */
    sci_open(kAppUart, kUartBaud, 0U);
    sci_open(kLinUart, kLinBaud, 2U);  /* 低波特率用更大的 CKS 分频。 */
    sci_write(kLogUart, "RA8P1 resources up\r\n");
    spi_open();
    iic_open();
    canfd_open(R_CANFD0);
    canfd_open(R_CANFD1);
    const uint32_t period = (kPclkdHz / kPwmHz) - 1U; /* 1 kHz 对应的 GPT 周期。 */
    g_pwm_period = period;                            /* 留给上位机改占空比。 */
    gpt_start(R_GPT1, 1U, period / 2U, true);         /* 通道号 1，A/B 都输出，A 相 50%。 */
    gpt_start(R_GPT12, 12U, period / 4U, false);      /* 通道号 12，只 A 相，25%。 */
    gpt_start(R_GPT10, 10U, period / 8U, false);      /* 通道号 10，只 A 相，12.5%。 */
    adc_open();
    dac_open();
    usb_open();
    eth_open();
    link_init();
}

/* 500 ms 周期任务：采样、演示总线，并在已连接时上报状态。 */
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
    /* 已连接后不再改 GPIO/DAC/PWM，避免冲掉上位机刚写的值。 */
    g_inputs = gpio_read();
    adc_sample();
    (void) spi_xfer(sample);          /* 演示 SPI 交换，不看返回值。 */
    (void) iic_write(0x50U, sample);  /* 向地址 0x50 写一个字节，总线上没有器件也无妨。 */
    if (canfd_send(R_CANFD0, 0x123U, &sample, 1U, false)) /* 自发的是经典 CAN，不是 FD。 */
    {
        hl_board_send_can_log(&g_link, 0U, 0x123U, &sample, 1U, 0); /* 未连接时函数内部会直接返回。 */
    }
    if (canfd_send(R_CANFD1, 0x321U, &sample, 1U, false))
    {
        hl_board_send_can_log(&g_link, 1U, 0x321U, &sample, 1U, 0);
    }
    lin_break_and_sync();
    hl_board_send_snapshot(&g_link); /* 已连接才真正发 Snapshot。 */
    log_line();                      /* 文本只走日志口。 */
}
} // namespace

/* 线程主体。每 20 ms 收一次串口；每 25 拍（约 500 ms）做一次外设轮询。 */
extern "C" void board_app_run(void)
{
    uint32_t phase = 0U;
    bringup();
    for (;;)
    {
        poll_link();                     /* 命令尽量在 20 ms 内被处理。 */
        if ((phase % 25U) == 0U)
        {
            poll();                      /* 采样和周期上报。 */
        }
        ++phase;
        vTaskDelay(pdMS_TO_TICKS(20));   /* 把 CPU 让给空闲任务。 */
    }
}
