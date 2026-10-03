/* RX71M 板级。引脚复用和方向。 */
#include "board_priv.hpp"

namespace board
{


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

} // namespace board
