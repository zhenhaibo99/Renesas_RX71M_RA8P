#include "board_resources.hpp"

extern "C"
{
#include "platform.h"
#include "FreeRTOS.h"
#include "task.h"
}

namespace
{
constexpr uint32_t kPclkbHz = 60000000UL;
constexpr uint32_t kUartBaud = 115200UL;
constexpr uint32_t kLinBaud = 19200UL;

volatile struct st_sci0 * const kLogUart = &SCI1;
volatile struct st_sci0 * const kAppUart = &SCI2;
volatile struct st_sci0 * const kLinUart = &SCI5;

uint32_t g_tick = 0U;
uint16_t g_adc0 = 0U;
uint16_t g_adc1 = 0U;
uint8_t g_inputs = 0U;

bool wait_true(volatile const unsigned char & reg, unsigned char mask, bool set, uint32_t spins)
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

void mstp_unlock(void)
{
    SYSTEM.PRCR.WORD = 0xA502U;
}

void mstp_lock(void)
{
    SYSTEM.PRCR.WORD = 0xA500U;
}

void modules_start(void)
{
    mstp_unlock();
    MSTP_SCI1 = 0U;
    MSTP_SCI2 = 0U;
    MSTP_SCI5 = 0U;
    MSTP_CAN0 = 0U;
    MSTP_CAN1 = 0U;
    MSTP_RSPI0 = 0U;
    MSTP_RIIC0 = 0U;
    MSTP_USB0 = 0U;
    MSTP_EDMAC0 = 0U;
    MSTP_S12AD = 0U;
    MSTP_DA = 0U;
    MSTP_MTU = 0U;
    mstp_lock();
}

void periph_pin(volatile unsigned char & pfs, unsigned char sel, volatile unsigned char & pmr, unsigned char bit)
{
    pfs = sel;
    pmr = static_cast<unsigned char>(pmr | static_cast<unsigned char>(1U << bit));
}

void gpio_dir_out(volatile unsigned char & pmr, volatile unsigned char & pdr, unsigned char bit)
{
    pmr = static_cast<unsigned char>(pmr & static_cast<unsigned char>(~(1U << bit)));
    pdr = static_cast<unsigned char>(pdr | static_cast<unsigned char>(1U << bit));
}

void gpio_dir_in(volatile unsigned char & pmr, volatile unsigned char & pdr, unsigned char bit)
{
    pmr = static_cast<unsigned char>(pmr & static_cast<unsigned char>(~(1U << bit)));
    pdr = static_cast<unsigned char>(pdr & static_cast<unsigned char>(~(1U << bit)));
}

void pins_apply(void)
{
    MPC.PWPR.BIT.B0WI = 0U;
    MPC.PWPR.BIT.PFSWE = 1U;

    periph_pin(MPC.P26PFS.BYTE, 0x0AU, PORT2.PMR.BYTE, 6U);
    periph_pin(MPC.P30PFS.BYTE, 0x0AU, PORT3.PMR.BYTE, 0U);
    periph_pin(MPC.P50PFS.BYTE, 0x0AU, PORT5.PMR.BYTE, 0U);
    periph_pin(MPC.P52PFS.BYTE, 0x0AU, PORT5.PMR.BYTE, 2U);

    periph_pin(MPC.P32PFS.BYTE, 0x10U, PORT3.PMR.BYTE, 2U);
    periph_pin(MPC.P33PFS.BYTE, 0x10U, PORT3.PMR.BYTE, 3U);
    periph_pin(MPC.P54PFS.BYTE, 0x10U, PORT5.PMR.BYTE, 4U);
    periph_pin(MPC.P55PFS.BYTE, 0x10U, PORT5.PMR.BYTE, 5U);

    periph_pin(MPC.PA5PFS.BYTE, 0x0DU, PORTA.PMR.BYTE, 5U);
    periph_pin(MPC.PA6PFS.BYTE, 0x0DU, PORTA.PMR.BYTE, 6U);
    periph_pin(MPC.PA7PFS.BYTE, 0x0DU, PORTA.PMR.BYTE, 7U);
    periph_pin(MPC.PA4PFS.BYTE, 0x0DU, PORTA.PMR.BYTE, 4U);

    periph_pin(MPC.P12PFS.BYTE, 0x0FU, PORT1.PMR.BYTE, 2U);
    periph_pin(MPC.P13PFS.BYTE, 0x0FU, PORT1.PMR.BYTE, 3U);

    periph_pin(MPC.PC3PFS.BYTE, 0x0AU, PORTC.PMR.BYTE, 3U);
    periph_pin(MPC.PC2PFS.BYTE, 0x0AU, PORTC.PMR.BYTE, 2U);

    periph_pin(MPC.P14PFS.BYTE, 0x01U, PORT1.PMR.BYTE, 4U);
    periph_pin(MPC.P17PFS.BYTE, 0x01U, PORT1.PMR.BYTE, 7U);
    periph_pin(MPC.P24PFS.BYTE, 0x01U, PORT2.PMR.BYTE, 4U);
    periph_pin(MPC.P25PFS.BYTE, 0x01U, PORT2.PMR.BYTE, 5U);

    periph_pin(MPC.P71PFS.BYTE, 0x11U, PORT7.PMR.BYTE, 1U);
    periph_pin(MPC.P72PFS.BYTE, 0x11U, PORT7.PMR.BYTE, 2U);
    periph_pin(MPC.P76PFS.BYTE, 0x12U, PORT7.PMR.BYTE, 6U);
    periph_pin(MPC.P77PFS.BYTE, 0x11U, PORT7.PMR.BYTE, 7U);
    periph_pin(MPC.P80PFS.BYTE, 0x11U, PORT8.PMR.BYTE, 0U);
    periph_pin(MPC.P81PFS.BYTE, 0x11U, PORT8.PMR.BYTE, 1U);
    periph_pin(MPC.P82PFS.BYTE, 0x11U, PORT8.PMR.BYTE, 2U);
    periph_pin(MPC.P83PFS.BYTE, 0x11U, PORT8.PMR.BYTE, 3U);
    periph_pin(MPC.P86PFS.BYTE, 0x11U, PORT8.PMR.BYTE, 6U);
    periph_pin(MPC.P87PFS.BYTE, 0x11U, PORT8.PMR.BYTE, 7U);
    MPC.PFENET.BIT.PHYMODE0 = 0U;

    gpio_dir_in(PORT0.PMR.BYTE, PORT0.PDR.BYTE, 5U);
    gpio_dir_in(PORT0.PMR.BYTE, PORT0.PDR.BYTE, 7U);
    gpio_dir_in(PORTE.PMR.BYTE, PORTE.PDR.BYTE, 0U);
    gpio_dir_in(PORTE.PMR.BYTE, PORTE.PDR.BYTE, 1U);
    PORT0.PCR.BYTE = static_cast<unsigned char>(PORT0.PCR.BYTE | static_cast<unsigned char>((1U << 5) | (1U << 7)));

    gpio_dir_out(PORT9.PMR.BYTE, PORT9.PDR.BYTE, 0U);
    gpio_dir_out(PORT9.PMR.BYTE, PORT9.PDR.BYTE, 1U);
    gpio_dir_out(PORT9.PMR.BYTE, PORT9.PDR.BYTE, 2U);
    gpio_dir_out(PORT9.PMR.BYTE, PORT9.PDR.BYTE, 3U);

    PORT4.PMR.BYTE = static_cast<unsigned char>(PORT4.PMR.BYTE & static_cast<unsigned char>(~0x03U));
    PORT4.PDR.BYTE = static_cast<unsigned char>(PORT4.PDR.BYTE & static_cast<unsigned char>(~0x03U));
    MPC.P40PFS.BIT.ASEL = 1U;
    MPC.P41PFS.BIT.ASEL = 1U;

    MPC.PWPR.BIT.PFSWE = 0U;
    MPC.PWPR.BIT.B0WI = 1U;
}

uint8_t sci_brr(uint32_t baud, uint32_t cks)
{
    const uint32_t div = (cks == 0U) ? 1U : 4U;
    const uint32_t den = div * 8U * baud;
    uint32_t brr = (kPclkbHz + (den / 2U)) / den;
    if (brr > 0U)
    {
        brr -= 1U;
    }
    if (brr > 255U)
    {
        brr = 255U;
    }
    return static_cast<uint8_t>(brr);
}

void sci_open(volatile struct st_sci0 * sci, uint32_t baud, uint32_t cks)
{
    sci->SCR.BYTE = 0U;
    sci->SMR.BYTE = static_cast<unsigned char>(cks & 0x03U);
    sci->SCMR.BYTE = 0xF2U;
    sci->SEMR.BIT.BGDM = 1U;
    sci->BRR = sci_brr(baud, cks);
    sci->SCR.BIT.RE = 1U;
    sci->SCR.BIT.TE = 1U;
}

void sci_putc(volatile struct st_sci0 * sci, uint8_t ch)
{
    if (!wait_true(sci->SSR.BYTE, 0x80U, true, 200000U))
    {
        return;
    }
    sci->TDR = ch;
}

void sci_write(volatile struct st_sci0 * sci, const char * text)
{
    while (*text != '\0')
    {
        sci_putc(sci, static_cast<uint8_t>(*text));
        ++text;
    }
}

int sci_getc(volatile struct st_sci0 * sci)
{
    if ((sci->SSR.BYTE & 0x38U) != 0U)
    {
        sci->SSR.BYTE = static_cast<unsigned char>(sci->SSR.BYTE & static_cast<unsigned char>(~0x38U));
        (void) sci->RDR;
    }
    if ((sci->SSR.BYTE & 0x40U) == 0U)
    {
        return -1;
    }
    return static_cast<int>(sci->RDR);
}

void lin_break_and_sync(void)
{
    kLinUart->SCR.BIT.TE = 0U;
    PORTC.PMR.BIT.B3 = 0U;
    PORTC.PDR.BIT.B3 = 1U;
    PORTC.PODR.BIT.B3 = 0U;
    R_BSP_SoftwareDelay(1U, BSP_DELAY_MILLISECS);
    PORTC.PODR.BIT.B3 = 1U;
    PORTC.PMR.BIT.B3 = 1U;
    kLinUart->SCR.BIT.TE = 1U;
    sci_putc(kLinUart, 0x55U);
}

void spi_open(void)
{
    RSPI0.SPCR.BYTE = 0U;
    RSPI0.SPPCR.BYTE = 0U;
    RSPI0.SPBR = 29U;
    RSPI0.SPDCR.BYTE = 0U;
    RSPI0.SPCMD0.BIT.SPB = 7U;
    RSPI0.SPCR.BIT.MSTR = 1U;
    RSPI0.SPCR.BIT.SPE = 1U;
}

uint8_t spi_xfer(uint8_t tx)
{
    uint32_t spins = 100000U;
    while ((RSPI0.SPSR.BIT.SPTEF == 0U) && (spins-- != 0U))
    {
    }
    RSPI0.SPDR.LONG = tx;
    spins = 100000U;
    while ((RSPI0.SPSR.BIT.SPRF == 0U) && (spins-- != 0U))
    {
    }
    return static_cast<uint8_t>(RSPI0.SPDR.LONG);
}

void iic_open(void)
{
    RIIC0.ICCR1.BIT.ICE = 0U;
    RIIC0.ICCR1.BIT.IICRST = 1U;
    RIIC0.ICCR1.BIT.ICE = 1U;
    RIIC0.ICMR1.BIT.CKS = 3U;
    RIIC0.ICBRH.BYTE = 36U;
    RIIC0.ICBRL.BYTE = 36U;
    RIIC0.ICFER.BIT.NFE = 1U;
    RIIC0.ICCR1.BIT.IICRST = 0U;
}

bool iic_write(uint8_t addr, uint8_t data)
{
    RIIC0.ICSR2.BYTE = static_cast<unsigned char>(RIIC0.ICSR2.BYTE & static_cast<unsigned char>(~0x10U));
    RIIC0.ICCR2.BYTE = 0x62U;
    if (!wait_true(RIIC0.ICSR2.BYTE, 0x80U, true, 100000U))
    {
        return false;
    }
    RIIC0.ICDRT = static_cast<unsigned char>(addr << 1);
    if (!wait_true(RIIC0.ICSR2.BYTE, 0x40U, true, 100000U))
    {
        RIIC0.ICCR2.BIT.SP = 1U;
        return false;
    }
    if ((RIIC0.ICSR2.BYTE & 0x10U) != 0U)
    {
        RIIC0.ICCR2.BIT.SP = 1U;
        return false;
    }
    RIIC0.ICDRT = data;
    (void) wait_true(RIIC0.ICSR2.BYTE, 0x40U, true, 100000U);
    RIIC0.ICCR2.BIT.SP = 1U;
    return true;
}

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

void can_open(volatile struct st_can & can)
{
    can.CTLR.BIT.SLPM = 0U;
    can.CTLR.BIT.CANM = 1U;
    if (!can_wait_reset(can, true))
    {
        return;
    }
    can.BCR.BIT.CCLKS = 0U;
    can.BCR.BIT.BRP = 11U;
    can.BCR.BIT.TSEG1 = 5U;
    can.BCR.BIT.TSEG2 = 2U;
    can.BCR.BIT.SJW = 0U;
    can.CTLR.BIT.CANM = 0U;
    (void) can_wait_reset(can, false);
}

void can_send(volatile struct st_can & can, uint16_t id, uint8_t data)
{
    if (can.MCTL[0].BIT.TX.TRMACTIVE != 0U)
    {
        return;
    }
    can.MCTL[0].BYTE = 0U;
    /* 标准帧 ID 在 SID[10:0]，位于 ID 寄存器 bit28-18。直接写整字，避免 11 位位域的截断警告。 */
    can.MB[0].ID.LONG = (static_cast<unsigned long>(id) & 0x7FFUL) << 18;
    can.MB[0].DLC = 1U;
    can.MB[0].DATA[0] = data;
    can.MCTL[0].BYTE = 0x80U;
}

void pwm_open(void)
{
    MTU.TSTRA.BIT.CST3 = 0U;
    MTU.TSTRA.BIT.CST4 = 0U;
    MTU3.TCR.BIT.CCLR = 1U;
    MTU3.TCR.BIT.TPSC = 0U;
    MTU3.TMDR1.BIT.MD = 2U;
    MTU3.TIORH.BIT.IOA = 1U;
    MTU3.TIORH.BIT.IOB = 2U;
    MTU3.TGRA = 59999U;
    MTU3.TGRB = 30000U;
    MTU3.TCNT = 0U;

    MTU4.TCR.BIT.CCLR = 1U;
    MTU4.TCR.BIT.TPSC = 0U;
    MTU4.TMDR1.BIT.MD = 2U;
    MTU4.TIORH.BIT.IOA = 1U;
    MTU4.TIORL.BIT.IOC = 1U;
    MTU4.TGRA = 59999U;
    MTU4.TGRB = 15000U;
    MTU4.TGRC = 7500U;
    MTU4.TCNT = 0U;

    MTU.TOERA.BIT.OE3B = 1U;
    MTU.TOERA.BIT.OE4A = 1U;
    MTU.TOERA.BIT.OE4C = 1U;
    MTU.TSTRA.BIT.CST3 = 1U;
    MTU.TSTRA.BIT.CST4 = 1U;
}

void adc_sample(void)
{
    S12AD.ADANSA0.WORD = 0x0003U;
    S12AD.ADCSR.BIT.ADCS = 0U;
    S12AD.ADCSR.BIT.ADST = 1U;
    uint32_t spins = 200000U;
    while ((S12AD.ADCSR.BIT.ADST != 0U) && (spins-- != 0U))
    {
    }
    g_adc0 = static_cast<uint16_t>(S12AD.ADDR0 & 0x0FFFU);
    g_adc1 = static_cast<uint16_t>(S12AD.ADDR1 & 0x0FFFU);
}

void dac_write(uint16_t code)
{
    DA.DADPR.BIT.DPSEL = 1U;
    DA.DADR0 = static_cast<unsigned short>(code & 0x0FFFU);
    DA.DACR.BIT.DAOE0 = 1U;
}

void usb_open(void)
{
    USB0.SYSCFG.WORD = static_cast<unsigned short>(USB0.SYSCFG.WORD | static_cast<unsigned short>(1U << 10));
    volatile uint32_t spins = 8000U;
    while (spins-- != 0U)
    {
    }
    USB0.SYSCFG.WORD = static_cast<unsigned short>(USB0.SYSCFG.WORD | static_cast<unsigned short>((1U << 0) | (1U << 4)));
}

void eth_open(void)
{
    EDMAC0.EDMR.BIT.SWR = 1U;
    uint32_t spins = 200000U;
    while ((EDMAC0.EDMR.BIT.SWR != 0U) && (spins-- != 0U))
    {
    }
    ETHERC0.MAHR = 0x02000000UL;
    ETHERC0.MALR.LONG = 0x00000001UL;
    ETHERC0.ECMR.BIT.DM = 1U;
    ETHERC0.ECMR.BIT.RE = 1U;
    ETHERC0.ECMR.BIT.TE = 1U;
}

void gpio_write(uint8_t mask)
{
    PORT9.PODR.BIT.B0 = (mask & 0x01U) != 0U;
    PORT9.PODR.BIT.B1 = (mask & 0x02U) != 0U;
    PORT9.PODR.BIT.B2 = (mask & 0x04U) != 0U;
    PORT9.PODR.BIT.B3 = (mask & 0x08U) != 0U;
}

uint8_t gpio_read(void)
{
    uint8_t value = 0U;
    if (PORT0.PIDR.BIT.B5 != 0U)
    {
        value = static_cast<uint8_t>(value | 0x01U);
    }
    if (PORT0.PIDR.BIT.B7 != 0U)
    {
        value = static_cast<uint8_t>(value | 0x02U);
    }
    if (PORTE.PIDR.BIT.B0 != 0U)
    {
        value = static_cast<uint8_t>(value | 0x04U);
    }
    if (PORTE.PIDR.BIT.B1 != 0U)
    {
        value = static_cast<uint8_t>(value | 0x08U);
    }
    return value;
}

void hex4(char * dst, uint16_t value)
{
    static const char kHex[] = "0123456789ABCDEF";
    dst[0] = kHex[(value >> 12) & 0x0FU];
    dst[1] = kHex[(value >> 8) & 0x0FU];
    dst[2] = kHex[(value >> 4) & 0x0FU];
    dst[3] = kHex[value & 0x0FU];
}

void hex2(char * dst, uint8_t value)
{
    static const char kHex[] = "0123456789ABCDEF";
    dst[0] = kHex[value >> 4];
    dst[1] = kHex[value & 0x0FU];
}

void log_line(void)
{
    char line[] = "RX t=0000 adc=0000/0000 in=00\r\n";
    hex4(&line[5], static_cast<uint16_t>(g_tick));
    hex4(&line[14], g_adc0);
    hex4(&line[19], g_adc1);
    hex2(&line[27], g_inputs);
    sci_write(kLogUart, line);
}

void bringup(void)
{
    modules_start();
    pins_apply();
    sci_open(kLogUart, kUartBaud, 0U);
    sci_open(kAppUart, kUartBaud, 0U);
    sci_open(kLinUart, kLinBaud, 1U);
    sci_write(kLogUart, "RX71M resources up\r\n");
    spi_open();
    iic_open();
    can_open(CAN0);
    can_open(CAN1);
    pwm_open();
    dac_write(0U);
    usb_open();
    eth_open();
}

void poll(void)
{
    ++g_tick;
    gpio_write(static_cast<uint8_t>(g_tick & 0x0FU));
    g_inputs = gpio_read();
    adc_sample();
    dac_write(static_cast<uint16_t>((g_tick << 4) & 0x0FFFU));
    (void) spi_xfer(static_cast<uint8_t>(g_tick));
    (void) iic_write(0x50U, static_cast<uint8_t>(g_tick));
    can_send(CAN0, 0x123U, static_cast<uint8_t>(g_tick));
    can_send(CAN1, 0x321U, static_cast<uint8_t>(g_tick));
    lin_break_and_sync();
    const int rx = sci_getc(kAppUart);
    if (rx >= 0)
    {
        sci_putc(kAppUart, static_cast<uint8_t>(rx));
    }
    log_line();
}
} // namespace

extern "C" void board_app_run(void)
{
    bringup();
    for (;;)
    {
        poll();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
