/* 在 PC 上对照 host_link.c 与 protoc 36.2 的黄金字节，并模拟两块板的命令分发。
 * 不链接固件，也不打开串口。ci/pipeline.ps1 -Stage protocol 会编译并运行本文件。
 */
#include "host_link.h"

#include <stdio.h>
#include <string.h>

static int g_fail; /* 任一用例失败后置 1，main 以此作为退出码。 */

/* 编码结果必须与期望字节逐字节相同。长度不同或内容不同都算失败。 */
static void expect_bytes(const char *name, const uint8_t *got, int n, const uint8_t *exp, int en)
{
    if ((n == en) && (memcmp(got, exp, (size_t)n) == 0))
    {
        printf("ok %s\n", name);
        return;
    }
    printf("FAIL %s got %d exp %d\n", name, n, en);
    g_fail = 1;
}

/* 编码后再解码，关键字段必须还原。0 被省略的状态量不在这里比较。 */
static void roundtrip(const char *name, const HlEnvelope *in)
{
    uint8_t buf[HL_MAX_PAYLOAD];
    HlEnvelope out;
    int n = hl_encode_envelope(in, buf, (int)sizeof buf);
    if ((n < 0) || (hl_decode_envelope(buf, n, &out) != 0))
    {
        printf("FAIL %s roundtrip encode\n", name);
        g_fail = 1;
        return;
    }
    if ((out.sequence != in->sequence) || (out.command != in->command) ||
        (out.has_snapshot != in->has_snapshot) || (out.has_set_outputs != in->has_set_outputs) ||
        (out.outputs_mask != in->outputs_mask) || (out.has_set_pwm != in->has_set_pwm) ||
        (out.pwm_channel != in->pwm_channel) || (out.pwm_duty_permille != in->pwm_duty_permille) ||
        (out.has_send_can != in->has_send_can) || (out.can_channel != in->can_channel) ||
        (out.can_id != in->can_id) || (out.can_dlc != in->can_dlc) || (out.can_req_fd != in->can_req_fd) ||
        (out.has_ack != in->has_ack) || (out.ack_status != in->ack_status) ||
        (strcmp(out.name, in->name) != 0) || (strcmp(out.ack_detail, in->ack_detail) != 0))
    {
        printf("FAIL %s roundtrip fields\n", name);
        g_fail = 1;
        return;
    }
    printf("ok %s roundtrip\n", name);
}

static uint8_t g_cap[HL_MAX_FRAME * 4]; /* 板卡“串口”上抓到的全部应答字节。 */
static int g_cap_n;
static uint8_t g_mask;       /* 回调记下的 GPIO 掩码。 */
static uint16_t g_dac;
static int g_pwm_ch;
static uint16_t g_pwm_duty;
static int g_can_fd;         /* 回调看到的 fd 标志。 */
static int g_can_rc;         /* 模拟发送邮箱：0 成功，非 0 表示忙。 */

/* 代替业务串口，把板卡发出的帧接住。 */
static void cap_write(void *ctx, const uint8_t *data, uint16_t len)
{
    (void)ctx;
    if (g_cap_n + len < (int)sizeof g_cap)
    {
        memcpy(g_cap + g_cap_n, data, len);
        g_cap_n += len;
    }
}

static void on_outputs(void *ctx, uint8_t mask)
{
    (void)ctx;
    g_mask = mask;
}

static void on_dac(void *ctx, uint16_t code)
{
    (void)ctx;
    g_dac = code;
}

static int on_pwm(void *ctx, uint8_t channel, uint16_t duty)
{
    (void)ctx;
    g_pwm_ch = channel;
    g_pwm_duty = duty;
    return 0; /* 测试里 PWM 始终成功。 */
}

static int on_can(void *ctx, uint8_t channel, uint32_t id, const uint8_t *data, uint8_t dlc, int fd)
{
    (void)ctx;
    (void)channel;
    (void)id;
    (void)data;
    (void)dlc;
    g_can_fd = fd;
    return g_can_rc;
}

/* 板卡填写状态时使用的固定内容，便于检查回调被调用。 */
static void on_fill(void *ctx, HlEnvelope *msg)
{
    (void)ctx;
    msg->tick = 9u;
    msg->board = HL_BOARD_RX71M;
    memcpy(msg->name, "RX71M", 6);
}

/* 把一整帧逐字节送进板卡解析器。 */
static void push_all(HlBoardLink *link, const uint8_t *frame, int n)
{
    int i;
    for (i = 0; i < n; ++i)
    {
        hl_board_push(link, frame[i]);
    }
}

/* 组一帧发给指定板，再从抓到的应答里核对 Ack 状态。can_fd 非 0 时模拟 RA8P。 */
static void board_case(const char *name, int can_fd, const HlEnvelope *req, uint32_t expect_status)
{
    HlBoardLink link;
    HlBoardOps ops;
    uint8_t payload[HL_MAX_PAYLOAD];
    uint8_t frame[HL_MAX_FRAME];
    HlParser parser;
    HlEnvelope reply;
    int n;
    int f;
    int i;
    memset(&ops, 0, sizeof ops);
    ops.write = cap_write;
    ops.set_outputs = on_outputs;
    ops.set_dac = on_dac;
    ops.set_pwm = on_pwm;
    ops.send_can = on_can;
    ops.fill_snapshot = on_fill;
    hl_board_init(&link, can_fd ? HL_BOARD_RA8P : HL_BOARD_RX71M, can_fd, &ops);
    n = hl_encode_envelope(req, payload, (int)sizeof payload);
    f = hl_encode_frame(0u, 0u, (uint8_t)req->sequence, payload, (uint16_t)n, frame, (int)sizeof frame); /* 板卡号 0：两块板都接受。 */
    g_cap_n = 0;
    push_all(&link, frame, f);
    hl_parser_init(&parser);
    for (i = 0; i < g_cap_n; ++i)
    {
        if (hl_parser_push(&parser, g_cap[i])) /* 抓到的第一帧完整且 CRC 正确。 */
        {
            if (hl_decode_envelope(parser.payload, (int)parser.len, &reply) != 0)
            {
                printf("FAIL %s reply decode\n", name);
                g_fail = 1;
                return;
            }
            if (!reply.has_ack || (reply.ack_status != expect_status))
            {
                printf("FAIL %s status %u exp %u detail %s\n", name, reply.ack_status, expect_status, reply.ack_detail);
                g_fail = 1;
                return;
            }
            printf("ok %s\n", name);
            return;
        }
    }
    printf("FAIL %s no reply\n", name);
    g_fail = 1;
}

int main(void)
{
    uint8_t buf[HL_MAX_PAYLOAD];
    HlEnvelope msg;
    /* 下列数组来自 protoc 36.2 --encode，与 samples/*.txt 对应。改协议时要重新生成。 */
    const uint8_t ping[] = {0x08, 0x01, 0x10, 0x01};
    const uint8_t outputs[] = {0x08, 0x02, 0x10, 0x03, 0x22, 0x02, 0x08, 0x00}; /* 掩码 0 仍在线上。 */
    const uint8_t canfd[] = {0x08, 0x04, 0x10, 0x06, 0x3A, 0x0C, 0x08, 0x01, 0x10, 0xA3, 0x02, 0x1A, 0x03, 0x01, 0x02, 0xFF, 0x20, 0x01};
    const uint8_t snap[] = {0x08, 0x03, 0x10, 0x02, 0x1A, 0x19, 0x08, 0x09, 0x10, 0xFF, 0x1F, 0x18, 0x01, 0x20, 0x0F, 0x28, 0x06, 0x30, 0x80, 0x10, 0x38, 0x02, 0x40, 0x01, 0x4A, 0x05, 'R', 'A', '8', 'P', '1'};
    const uint8_t ack[] = {0x08, 0x05, 0x10, 0x06, 0x4A, 0x18, 0x08, 0x06, 0x10, 0x01, 0x1A, 0x12, 'c', 'a', 'n', ' ', 'f', 'd', ' ', 'u', 'n', 's', 'u', 'p', 'p', 'o', 'r', 't', 'e', 'd'};
    const uint8_t pwm[] = {0x08, 0x06, 0x10, 0x05, 0x32, 0x04, 0x08, 0x00, 0x10, 0x00}; /* 通道 0、占空比 0。 */
    const uint8_t classic[] = {0x08, 0x07, 0x10, 0x06, 0x3A, 0x0A, 0x08, 0x00, 0x10, 0xA3, 0x02, 0x1A, 0x01, 0x11, 0x20, 0x00}; /* fd=false 也编码。 */
    int n;

    memset(&msg, 0, sizeof msg);
    msg.sequence = 1u;
    msg.command = HL_CMD_PING;
    n = hl_encode_envelope(&msg, buf, (int)sizeof buf);
    expect_bytes("ping", buf, n, ping, (int)sizeof ping);
    roundtrip("ping", &msg);

    memset(&msg, 0, sizeof msg);
    msg.sequence = 2u;
    msg.command = HL_CMD_SET_OUTPUTS;
    msg.has_set_outputs = 1;
    msg.outputs_mask = 0u; /* 全关。 */
    n = hl_encode_envelope(&msg, buf, (int)sizeof buf);
    expect_bytes("set_outputs", buf, n, outputs, (int)sizeof outputs);

    memset(&msg, 0, sizeof msg);
    msg.sequence = 4u;
    msg.command = HL_CMD_SEND_CAN;
    msg.has_send_can = 1;
    msg.can_channel = 1u;
    msg.can_id = 291u; /* 0x123。 */
    msg.can_data[0] = 0x01u;
    msg.can_data[1] = 0x02u;
    msg.can_data[2] = 0xFFu;
    msg.can_dlc = 3u;
    msg.can_req_fd = 1;
    n = hl_encode_envelope(&msg, buf, (int)sizeof buf);
    expect_bytes("send_can", buf, n, canfd, (int)sizeof canfd);
    roundtrip("send_can", &msg);

    memset(&msg, 0, sizeof msg);
    msg.sequence = 3u;
    msg.command = HL_CMD_SNAPSHOT;
    msg.has_snapshot = 1;
    msg.tick = 9u;
    msg.adc0 = 4095u;
    msg.adc1 = 1u;
    msg.inputs = 15u;
    msg.outputs = 6u;
    msg.dac = 2048u;
    msg.board = HL_BOARD_RA8P;
    msg.can_fd = 1;
    memcpy(msg.name, "RA8P1", 6);
    n = hl_encode_envelope(&msg, buf, (int)sizeof buf);
    expect_bytes("snapshot", buf, n, snap, (int)sizeof snap);
    roundtrip("snapshot", &msg);

    memset(&msg, 0, sizeof msg);
    msg.sequence = 5u;
    msg.command = HL_CMD_SEND_CAN;
    msg.has_ack = 1;
    msg.ack_command = HL_CMD_SEND_CAN;
    msg.ack_status = HL_ACK_UNSUPPORTED;
    memcpy(msg.ack_detail, "can fd unsupported", 19);
    n = hl_encode_envelope(&msg, buf, (int)sizeof buf);
    expect_bytes("ack", buf, n, ack, (int)sizeof ack);

    memset(&msg, 0, sizeof msg);
    msg.sequence = 6u;
    msg.command = HL_CMD_SET_PWM;
    msg.has_set_pwm = 1; /* 通道和占空比保持 0。 */
    n = hl_encode_envelope(&msg, buf, (int)sizeof buf);
    expect_bytes("set_pwm", buf, n, pwm, (int)sizeof pwm);

    memset(&msg, 0, sizeof msg);
    msg.sequence = 7u;
    msg.command = HL_CMD_SEND_CAN;
    msg.has_send_can = 1;
    msg.can_id = 291u;
    msg.can_data[0] = 0x11u;
    msg.can_dlc = 1u; /* can_req_fd 保持 0，表示经典 CAN。 */
    n = hl_encode_envelope(&msg, buf, (int)sizeof buf);
    expect_bytes("classic", buf, n, classic, (int)sizeof classic);

    g_can_rc = 0; /* 邮箱空闲。 */
    memset(&msg, 0, sizeof msg);
    msg.sequence = 8u;
    msg.command = HL_CMD_SEND_CAN;
    msg.has_send_can = 1;
    msg.can_channel = 0u;
    msg.can_id = 0x123u;
    msg.can_data[0] = 0x01u;
    msg.can_dlc = 1u;
    msg.can_req_fd = 1;
    board_case("rx rejects fd", 0, &msg, HL_ACK_UNSUPPORTED); /* RX 不发 CAN FD。 */
    board_case("ra accepts fd", 1, &msg, HL_ACK_OK);

    memset(&msg, 0, sizeof msg);
    msg.sequence = 9u;
    msg.command = HL_CMD_SET_OUTPUTS;
    msg.has_set_outputs = 1;
    msg.outputs_mask = 0u;
    board_case("rx clears gpio", 0, &msg, HL_ACK_OK);
    if (g_mask != 0u) /* 回调必须真正收到 0，不能把省略当成没写。 */
    {
        printf("FAIL gpio mask %u\n", g_mask);
        g_fail = 1;
    }
    else
    {
        printf("ok gpio zero\n");
    }

    return g_fail ? 1 : 0;
}
