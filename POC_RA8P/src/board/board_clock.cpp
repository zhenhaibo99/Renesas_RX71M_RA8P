/* RA8P 板级。解除模块停止。 */
#include "board_priv.hpp"

namespace board
{


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

} // namespace board
