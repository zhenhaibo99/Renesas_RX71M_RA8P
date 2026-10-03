/* RA8P 板级。上电顺序、500 ms 轮询和应用任务。 */
#include "board_priv.hpp"

namespace board
{


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
    iwdt_open(); /* 上电完成后再开始计时。 */
    log_write(kLogInfo, "iwdt on");
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

} // namespace board

using namespace board;


/* 线程主体。每 20 ms 收一次串口；每 25 拍（约 500 ms）做一次外设轮询。 */
extern "C" void board_app_run(void)
{
    uint32_t phase = 0U;
    bringup();
    for (;;)
    {
        iwdt_refresh(); /* 先喂狗，再做本拍的串口和外设。 */
        poll_link();                     /* 命令尽量在 20 ms 内被处理。 */
        if ((phase % 25U) == 0U)
        {
            poll();                      /* 采样和周期上报。 */
        }
        ++phase;
        vTaskDelay(pdMS_TO_TICKS(20));   /* 把 CPU 让给空闲任务。 */
    }
}
