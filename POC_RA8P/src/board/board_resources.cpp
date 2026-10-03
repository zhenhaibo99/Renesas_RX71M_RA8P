#include "board_resources.hpp"

#include "bsp_api.h"
#include "r_ioport.h"
#include "FreeRTOS.h"
#include "task.h"

namespace
{
constexpr uint32_t kPclkaHz = 125000000UL;
constexpr uint32_t kPclkdHz = 250000000UL;
constexpr uint32_t kUartBaud = 115200UL;
constexpr uint32_t kLinBaud = 19200UL;
constexpr uint32_t kPwmHz = 1000UL;

constexpr uint32_t kPeriph = IOPORT_CFG_PERIPHERAL_PIN;
constexpr uint32_t kSciEven = IOPORT_PERIPHERAL_SCI0_2_4_6_8;
constexpr uint32_t kSciOdd = IOPORT_PERIPHERAL_SCI1_3_5_7_9;

R_SCI_B0_Type * const kLogUart = R_SCI_B8;
R_SCI_B0_Type * const kAppUart = R_SCI_B7;
R_SCI_B0_Type * const kLinUart = R_SCI_B2;

uint32_t g_tick = 0U;
uint16_t g_adc0 = 0U;
uint16_t g_adc1 = 0U;
uint8_t g_inputs = 0U;

bool wait_flag(volatile const uint32_t & reg, uint32_t mask, bool set, uint32_t spins)
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

void pins_apply(void)
{
    R_BSP_PinAccessEnable();

    const uint32_t uart_tx = kPeriph | IOPORT_CFG_DRIVE_MID;
    const uint32_t uart_rx = kPeriph | IOPORT_CFG_PULLUP_ENABLE;
    R_BSP_PinCfg(BSP_IO_PORT_13_PIN_02, uart_tx | kSciEven);
    R_BSP_PinCfg(BSP_IO_PORT_13_PIN_03, uart_rx | kSciEven);
    R_BSP_PinCfg(BSP_IO_PORT_08_PIN_09, uart_tx | kSciOdd);
    R_BSP_PinCfg(BSP_IO_PORT_08_PIN_08, uart_rx | kSciOdd);
    R_BSP_PinCfg(BSP_IO_PORT_08_PIN_01, uart_tx | kSciEven);
    R_BSP_PinCfg(BSP_IO_PORT_08_PIN_02, uart_rx | kSciEven);

    const uint32_t can = kPeriph | IOPORT_PERIPHERAL_CAN | IOPORT_CFG_DRIVE_MID;
    R_BSP_PinCfg(BSP_IO_PORT_03_PIN_13, can);
    R_BSP_PinCfg(BSP_IO_PORT_03_PIN_12, can | IOPORT_CFG_PULLUP_ENABLE);
    R_BSP_PinCfg(BSP_IO_PORT_06_PIN_09, can);
    R_BSP_PinCfg(BSP_IO_PORT_06_PIN_10, can | IOPORT_CFG_PULLUP_ENABLE);

    const uint32_t spi = kPeriph | IOPORT_PERIPHERAL_SPI | IOPORT_CFG_DRIVE_MID;
    R_BSP_PinCfg(BSP_IO_PORT_01_PIN_00, spi);
    R_BSP_PinCfg(BSP_IO_PORT_01_PIN_01, spi);
    R_BSP_PinCfg(BSP_IO_PORT_01_PIN_02, spi);
    R_BSP_PinCfg(BSP_IO_PORT_01_PIN_03, spi);

    const uint32_t iic = kPeriph | IOPORT_PERIPHERAL_IIC | IOPORT_CFG_NMOS_ENABLE | IOPORT_CFG_PULLUP_ENABLE;
    R_BSP_PinCfg(BSP_IO_PORT_04_PIN_00, iic);
    R_BSP_PinCfg(BSP_IO_PORT_04_PIN_01, iic);

    const uint32_t gpt = kPeriph | IOPORT_PERIPHERAL_GPT1 | IOPORT_CFG_DRIVE_MID;
    R_BSP_PinCfg(BSP_IO_PORT_01_PIN_05, gpt);
    R_BSP_PinCfg(BSP_IO_PORT_01_PIN_04, gpt);
    R_BSP_PinCfg(BSP_IO_PORT_05_PIN_01, gpt);
    R_BSP_PinCfg(BSP_IO_PORT_08_PIN_10, gpt);

    const uint32_t din = IOPORT_CFG_PULLUP_ENABLE;
    R_BSP_PinCfg(BSP_IO_PORT_00_PIN_08, din);
    R_BSP_PinCfg(BSP_IO_PORT_00_PIN_09, din);
    R_BSP_PinCfg(BSP_IO_PORT_00_PIN_12, din);
    R_BSP_PinCfg(BSP_IO_PORT_04_PIN_10, din);

    const uint32_t dout = IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_LOW;
    R_BSP_PinCfg(BSP_IO_PORT_03_PIN_03, dout);
    R_BSP_PinCfg(BSP_IO_PORT_06_PIN_00, dout);
    R_BSP_PinCfg(BSP_IO_PORT_10_PIN_07, dout);
    R_BSP_PinCfg(BSP_IO_PORT_04_PIN_09, dout);

    R_BSP_PinCfg(BSP_IO_PORT_00_PIN_01, IOPORT_CFG_ANALOG_ENABLE);
    R_BSP_PinCfg(BSP_IO_PORT_00_PIN_07, IOPORT_CFG_ANALOG_ENABLE);
    R_BSP_PinCfg(BSP_IO_PORT_00_PIN_14, IOPORT_CFG_ANALOG_ENABLE);

    const uint32_t usb = kPeriph | IOPORT_PERIPHERAL_USB_FS | IOPORT_CFG_DRIVE_HIGH;
    R_BSP_PinCfg(BSP_IO_PORT_08_PIN_14, usb);
    R_BSP_PinCfg(BSP_IO_PORT_08_PIN_15, usb);
    R_BSP_PinCfg(BSP_IO_PORT_04_PIN_07, usb);
    R_BSP_PinCfg(BSP_IO_PORT_05_PIN_00, dout);

    const uint32_t rgmii = kPeriph | IOPORT_PERIPHERAL_ETHER_RGMII | IOPORT_CFG_DRIVE_HIGH;
    R_BSP_PinCfg(BSP_IO_PORT_03_PIN_04, rgmii);
    R_BSP_PinCfg(BSP_IO_PORT_03_PIN_05, rgmii);
    R_BSP_PinCfg(BSP_IO_PORT_03_PIN_06, rgmii);
    R_BSP_PinCfg(BSP_IO_PORT_03_PIN_07, rgmii);
    R_BSP_PinCfg(BSP_IO_PORT_03_PIN_10, rgmii);
    R_BSP_PinCfg(BSP_IO_PORT_03_PIN_09, rgmii);
    R_BSP_PinCfg(BSP_IO_PORT_09_PIN_06, rgmii);
    R_BSP_PinCfg(BSP_IO_PORT_09_PIN_07, rgmii);
    R_BSP_PinCfg(BSP_IO_PORT_09_PIN_08, rgmii);
    R_BSP_PinCfg(BSP_IO_PORT_09_PIN_09, rgmii);
    R_BSP_PinCfg(BSP_IO_PORT_02_PIN_06, rgmii);
    R_BSP_PinCfg(BSP_IO_PORT_09_PIN_05, rgmii);
    const uint32_t mdio = kPeriph | IOPORT_PERIPHERAL_ETHER_MII | IOPORT_CFG_DRIVE_MID;
    R_BSP_PinCfg(BSP_IO_PORT_04_PIN_15, mdio);
    R_BSP_PinCfg(BSP_IO_PORT_04_PIN_14, mdio | IOPORT_CFG_PULLUP_ENABLE);
    R_BSP_PinCfg(BSP_IO_PORT_07_PIN_08, IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_HIGH);

    R_BSP_PinAccessDisable();
}

void modules_start(void)
{
    R_BSP_RegisterProtectDisable(BSP_REG_PROTECT_OM_LPC_BATT);
    R_MSTP->MSTPCRB &= ~((1UL << (31U - 8U)) | (1UL << (31U - 7U)) | (1UL << (31U - 2U)) |
                         (1UL << (19U - 1U)) | (1UL << 9U) | (1UL << 11U) | (1UL << 15U) | (1UL << 14U));
    R_MSTP->MSTPCRC &= ~((1UL << 27U) | (1UL << 26U));
    R_MSTP->MSTPCRD &= ~((1UL << 21U) | (1UL << 20U));
    R_MSTP->MSTPCRE &= ~((1UL << (31U - 1U)) | (1UL << (31U - 10U)) | (1UL << (31U - 12U)));
    (void) R_MSTP->MSTPCRB;
    R_BSP_RegisterProtectEnable(BSP_REG_PROTECT_OM_LPC_BATT);
    R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MILLISECONDS);
}

uint8_t sci_brr(uint32_t baud, uint32_t cks)
{
    const uint32_t div = (cks == 0U) ? 1U : ((cks == 1U) ? 4U : 16U);
    const uint32_t den = div * 8U * baud;
    uint32_t brr = (kPclkaHz + (den / 2U)) / den;
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

void sci_open(R_SCI_B0_Type * sci, uint32_t baud, uint32_t cks)
{
    sci->CCR0 = 0U;
    sci->CCR3 = (1UL << 8);
    sci->CCR2 = (1UL << 4) | (static_cast<uint32_t>(sci_brr(baud, cks)) << 8) | (cks << 20);
    sci->CCR1 = (1UL << 28);
    sci->CCR0 = (1UL << 0) | (1UL << 4);
}

void sci_putc(R_SCI_B0_Type * sci, uint8_t ch)
{
    if (!wait_flag(sci->CSR, 1UL << 29, true, 200000U))
    {
        return;
    }
    sci->TDR_BY = ch;
}

void sci_write(R_SCI_B0_Type * sci, const char * text)
{
    while (*text != '\0')
    {
        sci_putc(sci, static_cast<uint8_t>(*text));
        ++text;
    }
}

int sci_getc(R_SCI_B0_Type * sci)
{
    if ((sci->CSR & (1UL << 24)) != 0U)
    {
        (void) sci->RDR_BY;
    }
    if ((sci->CSR & (1UL << 31)) == 0U)
    {
        return -1;
    }
    return static_cast<int>(sci->RDR_BY);
}

void lin_break_and_sync(void)
{
    kLinUart->CCR1_b.SPB2DT = 0U;
    kLinUart->CCR1_b.SPB2IO = 1U;
    R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MILLISECONDS);
    kLinUart->CCR1_b.SPB2IO = 0U;
    sci_putc(kLinUart, 0x55U);
}

void spi_open(void)
{
    R_SPI1->SPCR = 0U;
    R_SPI1->SPPCR = 0U;
    R_SPI1->SPBR = 61U;
    R_SPI1->SPSCR = 0U;
    R_SPI1->SPCMD[0] = static_cast<uint16_t>(7U << 8);
    R_SPI1->SPCR = static_cast<uint8_t>((1U << 3) | (1U << 6));
}

uint8_t spi_xfer(uint8_t tx)
{
    if (!wait_u8(R_SPI1->SPSR, static_cast<uint8_t>(1U << 5), true, 100000U))
    {
        return 0U;
    }
    R_SPI1->SPDR_BY = tx;
    if (!wait_u8(R_SPI1->SPSR, static_cast<uint8_t>(1U << 7), true, 100000U))
    {
        return 0U;
    }
    return R_SPI1->SPDR_BY;
}

void iic_open(void)
{
    R_IIC0->ICCR1_b.ICE = 0U;
    R_IIC0->ICCR1_b.IICRST = 1U;
    R_IIC0->ICCR1_b.ICE = 1U;
    R_IIC0->ICMR1_b.CKS = 3U;
    R_IIC0->ICBRH = 38U;
    R_IIC0->ICBRL = 38U;
    R_IIC0->ICMR3 = 0x01U;
    R_IIC0->ICFER = 0x30U;
    R_IIC0->ICCR1_b.IICRST = 0U;
}

bool iic_write(uint8_t addr, uint8_t data)
{
    R_IIC0->ICSR2 = static_cast<uint8_t>(R_IIC0->ICSR2 & static_cast<uint8_t>(~0x10U));
    R_IIC0->ICCR2 = 0x62U;
    if (!wait_u8(R_IIC0->ICSR2, 0x80U, true, 100000U))
    {
        return false;
    }
    R_IIC0->ICDRT = static_cast<uint8_t>(addr << 1);
    if (!wait_u8(R_IIC0->ICSR2, 0x40U, true, 100000U))
    {
        R_IIC0->ICCR2_b.SP = 1U;
        return false;
    }
    if ((R_IIC0->ICSR2 & 0x10U) != 0U)
    {
        R_IIC0->ICCR2_b.SP = 1U;
        return false;
    }
    R_IIC0->ICDRT = data;
    (void) wait_u8(R_IIC0->ICSR2, 0x40U, true, 100000U);
    R_IIC0->ICCR2_b.SP = 1U;
    return true;
}

bool canfd_leave_sleep(R_CANFD_Type * can)
{
    can->CFDGCTR_b.GSLPR = 0U;
    if (!wait_flag(can->CFDGSTS, 1UL << 2, false, 200000U))
    {
        return false;
    }
    can->CFDGCTR_b.GMDC = 1U;
    if (!wait_flag(can->CFDGSTS, 1UL << 0, true, 200000U))
    {
        return false;
    }
    can->CFDC[0].CTR_b.CSLPR = 0U;
    if (!wait_flag(can->CFDC[0].STS, 1UL << 2, false, 200000U))
    {
        return false;
    }
    can->CFDC[0].CTR_b.CHMDC = 1U;
    return wait_flag(can->CFDC[0].STS, 1UL << 0, true, 200000U);
}

void canfd_open(R_CANFD_Type * can)
{
    if (!canfd_leave_sleep(can))
    {
        return;
    }
    /* 48 MHz / 6 = 8 MHz，16 Tq → 500 kbit/s。NBRP=5, NSJW=2, NTSEG1=10, NTSEG2=3。 */
    constexpr uint32_t ncfg = 5UL | (2UL << 10) | (10UL << 17) | (3UL << 25);
    constexpr uint32_t dcfg = 5UL | (10UL << 8) | (3UL << 16) | (2UL << 24);
    can->CFDC[0].NCFG = ncfg;
    can->CFDC2[0].DCFG = dcfg;
    can->CFDGCTR_b.GMDC = 0U;
    can->CFDC[0].CTR_b.CHMDC = 0U;
    (void) wait_flag(can->CFDGSTS, 1UL << 0, false, 200000U);
}

void canfd_send(R_CANFD_Type * can, uint32_t id, uint8_t data)
{
    if ((can->CFDTMC[0] & 0x01U) != 0U)
    {
        return;
    }
    can->CFDTM[0].ID = id << 18;
    can->CFDTM[0].PTR = 1UL << 28;
    can->CFDTM[0].FDCTR = 0U;
    can->CFDTM[0].DF[0] = data;
    can->CFDTMC[0] = 0x01U;
}

void gpt_start(R_GPT0_Type * gpt, uint32_t channel, uint32_t duty, bool both)
{
    gpt->GTWP = 0xA500U;
    R_GPT0->GTSTP = 1UL << channel;
    gpt->GTCR = 0U;
    gpt->GTPR = (kPclkdHz / kPwmHz) - 1U;
    gpt->GTCCR[0] = duty;
    uint32_t ior = 6UL | (1UL << 8);
    if (both)
    {
        gpt->GTCCR[1] = duty / 2U;
        ior |= (6UL << 16) | (1UL << 24);
    }
    gpt->GTIOR = ior;
    gpt->GTWP = 0xA500U;
    R_GPT0->GTSTR = 1UL << channel;
}

void adc_open(void)
{
    R_ADC_B->ADCLKENR_b.CLKEN = 1U;
    (void) wait_flag(R_ADC_B->ADCLKSR, 1UL, true, 200000U);
    R_ADC_B->ADCLKCR = (2UL << 0) | (2UL << 16);
    R_ADC_B->ADSSTR0 = (0x20UL << 0) | (0x20UL << 16);
    R_ADC_B->ADCHCR0 = (1UL << 8);
    R_ADC_B->ADCHCR1 = (7UL << 8);
}

void adc_sample(void)
{
    R_ADC_B->ADSTR[0] = 1U;
    R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MILLISECONDS);
    g_adc0 = static_cast<uint16_t>(R_ADC_B->ADDR[0] & 0xFFFFU);
    g_adc1 = static_cast<uint16_t>(R_ADC_B->ADDR[1] & 0xFFFFU);
}

void dac_open(void)
{
    R_DAC_B0->DACR1_b.DPSEL = 1U;
    R_DAC_B0->DADR = 0U;
    R_DAC_B0->DACR0_b.DAOUTDIS = 0U;
    R_DAC_B0->DACR0_b.DACEN = 1U;
}

void dac_write(uint16_t code)
{
    R_DAC_B0->DADR = static_cast<uint16_t>(code & 0x0FFFU);
}

void usb_open(void)
{
    R_USB_FS0->SYSCFG_b.SCKE = 1U;
    R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MILLISECONDS);
    R_USB_FS0->SYSCFG_b.DCFM = 0U;
    R_USB_FS0->SYSCFG_b.DRPD = 0U;
    R_USB_FS0->SYSCFG_b.USBE = 1U;
    R_USB_FS0->SYSCFG_b.DPRPU = 1U;
}

void eth_open(void)
{
    R_ETHERC_EDMAC->EDMR_b.SWR = 1U;
    (void) wait_flag(R_ETHERC_EDMAC->EDMR, 1UL, false, 200000U);
}

void gpio_write(uint8_t mask)
{
    const bsp_io_port_pin_t pins[4] = {
        BSP_IO_PORT_03_PIN_03, BSP_IO_PORT_06_PIN_00, BSP_IO_PORT_10_PIN_07, BSP_IO_PORT_04_PIN_09};
    R_BSP_PinAccessEnable();
    for (uint32_t i = 0U; i < 4U; ++i)
    {
        R_BSP_PinWrite(pins[i], ((mask & (1U << i)) != 0U) ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW);
    }
    R_BSP_PinAccessDisable();
}

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
    char line[] = "RA t=0000 adc=0000/0000 in=00\r\n";
    hex4(&line[5], static_cast<uint16_t>(g_tick));
    hex4(&line[14], g_adc0);
    hex4(&line[19], g_adc1);
    hex2(&line[27], g_inputs);
    sci_write(kLogUart, line);
}

void bringup(void)
{
    pins_apply();
    modules_start();
    sci_open(kLogUart, kUartBaud, 0U);
    sci_open(kAppUart, kUartBaud, 0U);
    sci_open(kLinUart, kLinBaud, 2U);
    sci_write(kLogUart, "RA8P1 resources up\r\n");
    spi_open();
    iic_open();
    canfd_open(R_CANFD0);
    canfd_open(R_CANFD1);
    const uint32_t period = (kPclkdHz / kPwmHz) - 1U;
    gpt_start(R_GPT1, 1U, period / 2U, true);
    gpt_start(R_GPT12, 12U, period / 4U, false);
    gpt_start(R_GPT10, 10U, period / 8U, false);
    adc_open();
    dac_open();
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
    canfd_send(R_CANFD0, 0x123U, static_cast<uint8_t>(g_tick));
    canfd_send(R_CANFD1, 0x321U, static_cast<uint8_t>(g_tick));
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
