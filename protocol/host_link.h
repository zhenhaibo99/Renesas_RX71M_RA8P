#ifndef HOST_LINK_H_
#define HOST_LINK_H_

#include <stdint.h>

/*
 * 私有帧 HL1，载荷是 host_link.proto（Protobuf 36.2 / proto3）的 Envelope。
 *
 *   0  A5
 *   1  5A
 *   2  版本，当前为 1
 *   3  板卡：0 不限，1 RX71M，2 RA8P（与 BoardKind 相同）
 *   4  标志：bit0 板卡发出，bit1 该板支持 CAN FD
 *   5  序号低 8 位
 *   6  载荷长度，小端
 *   8  载荷
 *      CRC16/CCITT-FALSE（多项式 0x1021，初值 0xFFFF），覆盖版本到载荷，CRC 小端
 *
 * RX71M 与 RA8P 用同一帧。板卡号用来区分两类板；CAN FD 只在 RA8P 上执行。
 */

#ifdef __cplusplus
extern "C" {
#endif

#define HL_MAGIC0 0xA5u
#define HL_MAGIC1 0x5Au
#define HL_VERSION 1u

#define HL_BOARD_ANY 0u
#define HL_BOARD_RX71M 1u
#define HL_BOARD_RA8P 2u

#define HL_FLAG_FROM_BOARD 0x01u
#define HL_FLAG_CAN_FD 0x02u

#define HL_CMD_UNSPECIFIED 0u
#define HL_CMD_PING 1u
#define HL_CMD_SNAPSHOT 2u
#define HL_CMD_SET_OUTPUTS 3u
#define HL_CMD_SET_DAC 4u
#define HL_CMD_SET_PWM 5u
#define HL_CMD_SEND_CAN 6u

#define HL_ACK_OK 0u
#define HL_ACK_UNSUPPORTED 1u
#define HL_ACK_BAD_ARG 2u

#define HL_NAME_MAX 16
#define HL_DETAIL_MAX 40
#define HL_CAN_MAX 8
#define HL_MAX_PAYLOAD 192
#define HL_MAX_FRAME (10 + HL_MAX_PAYLOAD)

typedef struct HlEnvelope {
    uint32_t sequence;
    uint32_t command;
    int has_snapshot;
    uint32_t tick;
    uint32_t adc0;
    uint32_t adc1;
    uint32_t inputs;
    uint32_t outputs;
    uint32_t dac;
    uint32_t board;
    int can_fd;
    char name[HL_NAME_MAX];
    int has_set_outputs;
    uint32_t outputs_mask;
    int has_set_dac;
    uint32_t dac_code;
    int has_set_pwm;
    uint32_t pwm_channel;
    uint32_t pwm_duty_permille;
    int has_send_can;
    uint32_t can_channel;
    uint32_t can_id;
    uint8_t can_data[HL_CAN_MAX];
    uint8_t can_dlc;
    int can_req_fd;
    int has_can_log;
    uint32_t log_channel;
    uint32_t log_id;
    uint8_t log_data[HL_CAN_MAX];
    uint8_t log_dlc;
    int log_fd;
    int log_tx;
    int has_ack;
    uint32_t ack_command;
    uint32_t ack_status;
    char ack_detail[HL_DETAIL_MAX];
} HlEnvelope;

typedef struct HlParser {
    uint8_t state;
    uint8_t board;
    uint8_t flags;
    uint8_t seq;
    uint16_t len;
    uint16_t got;
    uint8_t crc_lo;
    uint8_t prefix[6];
    uint8_t payload[HL_MAX_PAYLOAD];
} HlParser;

int hl_encode_envelope(const HlEnvelope *msg, uint8_t *out, int cap);
int hl_decode_envelope(const uint8_t *in, int len, HlEnvelope *msg);
int hl_encode_frame(uint8_t board, uint8_t flags, uint8_t seq,
                    const uint8_t *payload, uint16_t len,
                    uint8_t *out, int cap);
uint16_t hl_crc16(const uint8_t *data, int len);

void hl_parser_init(HlParser *parser);
int hl_parser_push(HlParser *parser, uint8_t byte);

typedef void (*HlWriteFn)(void *ctx, const uint8_t *data, uint16_t len);
typedef void (*HlSetOutputsFn)(void *ctx, uint8_t mask);
typedef void (*HlSetDacFn)(void *ctx, uint16_t code);
typedef int (*HlSetPwmFn)(void *ctx, uint8_t channel, uint16_t duty_permille);
typedef int (*HlSendCanFn)(void *ctx, uint8_t channel, uint32_t id,
                           const uint8_t *data, uint8_t dlc, int fd);
typedef void (*HlFillFn)(void *ctx, HlEnvelope *msg);

typedef struct HlBoardOps {
    void *ctx;
    HlWriteFn write;
    HlSetOutputsFn set_outputs;
    HlSetDacFn set_dac;
    HlSetPwmFn set_pwm;
    HlSendCanFn send_can;
    HlFillFn fill_snapshot;
} HlBoardOps;

typedef struct HlBoardLink {
    HlParser parser;
    HlBoardOps ops;
    uint8_t board_id;
    uint8_t flags_tx;
    int linked;
    uint32_t tx_seq;
    uint8_t frame_buf[HL_MAX_FRAME];
    HlEnvelope req;
    HlEnvelope rsp;
} HlBoardLink;

void hl_board_init(HlBoardLink *link, uint8_t board_id, int can_fd, const HlBoardOps *ops);
void hl_board_push(HlBoardLink *link, uint8_t byte);
void hl_board_send_snapshot(HlBoardLink *link);
void hl_board_send_can_log(HlBoardLink *link, uint8_t channel, uint32_t id,
                           const uint8_t *data, uint8_t dlc, int fd);

#ifdef __cplusplus
}
#endif

#endif
