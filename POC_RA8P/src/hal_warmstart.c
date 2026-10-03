/*
* Copyright (c) 2020 - 2026 Renesas Electronics Corporation and/or its affiliates
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#include "hal_data.h"

FSP_CPP_HEADER
void R_BSP_WarmStart(bsp_warm_start_event_t event);

FSP_CPP_FOOTER

/*******************************************************************************************************************//**
 * 启动过程中的用户钩子。复位后、时钟就绪后、C 运行库就绪后各进来一次。
 * 这里在 C 运行库就绪后打开 IOPORT，让后续引脚配置可以写 PFS。
 *
 * @param[in]  event    当前处于启动的哪一步
 **********************************************************************************************************************/
void R_BSP_WarmStart (bsp_warm_start_event_t event)
{
    /* 刚出复位、时钟和 C 库都还没建好。 */
    if (BSP_WARM_START_RESET == event)
    {
#if BSP_FEATURE_FLASH_LP_VERSION != 0

        /* 允许读数据闪存。本芯片若没有 LP 闪存，这段不会编译进来。 */
        R_FACI_LP->DFLCTL = 1U;

        /* 正常要等 tDSTOP（约 6 us）数据闪存才恢复。
         * 放在时钟和 C 库初始化之前，后面的初始化通常超过 6 us，因此不再单独延时。 */
#endif
    }

#if BSP_CFG_OSPI_B_STARTUP_ENABLED && defined(BSP_CFG_OSPI_B_STARTUP_FN)
    /* 时钟已经切好，可以初始化片上 OSPI 闪存。当前工程默认不走这条。 */
    if (BSP_WARM_START_POST_CLOCK == event)
    {
        R_BSP_OspiBInit(BSP_CFG_OSPI_B_STARTUP_FN, true);
    }
#endif

    /* C 运行库和系统时钟都已就绪，可以调用 FSP。 */
    if (BSP_WARM_START_POST_C == event)
    {
        /* 按 configuration.xml 打开 IOPORT，后续 R_BSP_PinCfg 才能写引脚。 */
        R_IOPORT_Open(&IOPORT_CFG_CTRL, &IOPORT_CFG_NAME);

#if BSP_CFG_SDRAM_ENABLED

        /* SDRAM 必须在引脚配置之后初始化。当前工程默认关闭。 */
        R_BSP_SdramInit(true);
#endif
    }
}
