/* RX71M 板级。HL1 回调和业务口收包。 */
#include "board_priv.hpp"

/* HL1 帧和 Protobuf 编解码，只在本文件编入一次。 */
#include "../../../protocol/host_link.c"


namespace board
{


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
    const int was_linked = g_link.linked; /* 本拍之前是否已经连上。 */
    while ((rx = sci_getc(kAppUart)) >= 0)
    {
        hl_board_push(&g_link, static_cast<uint8_t>(rx));
    }
    if ((was_linked == 0) && (g_link.linked != 0))
    {
        log_write(kLogInfo, "link up"); /* 第一次合法帧。只打这一次。 */
    }
}

} // namespace board
