/* RA8P 板级。HL1 回调和业务口收包。 */
#include "board_priv.hpp"

/* HL1 帧和 Protobuf 编解码，只在本文件编入一次。 */
#include "../../../protocol/host_link.c"


namespace board
{


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

} // namespace board
