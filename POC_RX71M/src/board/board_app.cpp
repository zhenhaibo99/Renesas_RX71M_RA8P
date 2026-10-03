/* RX71M 板级。上电顺序、500 ms 轮询和应用任务。 */
#include "board_priv.hpp"

namespace board
{


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

} // namespace board

using namespace board;


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
