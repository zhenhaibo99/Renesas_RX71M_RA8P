/* RA8P 板级。按资源表配置引脚。 */
#include "board_priv.hpp"

namespace board
{


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

} // namespace board
