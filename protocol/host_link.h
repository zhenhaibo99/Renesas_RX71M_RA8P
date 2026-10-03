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

#define HL_MAGIC0 0xA5u                 /* 帧头第一字节。 */
#define HL_MAGIC1 0x5Au                 /* 帧头第二字节。 */
#define HL_VERSION 1u                   /* 当前帧版本。不对就丢弃。 */

#define HL_BOARD_ANY 0u                 /* 不限板卡，两块板都接受。 */
#define HL_BOARD_RX71M 1u               /* 与 BoardKind 的 RX 相同。 */
#define HL_BOARD_RA8P 2u                /* 与 BoardKind 的 RA 相同。 */

#define HL_FLAG_FROM_BOARD 0x01u        /* bit0：这帧由板卡发出。 */
#define HL_FLAG_CAN_FD 0x02u            /* bit1：该板支持 CAN FD。RX 不置。 */

#define HL_CMD_UNSPECIFIED 0u           /* 不作为请求。 */
#define HL_CMD_PING 1u                  /* 探活，回 Snapshot。 */
#define HL_CMD_SNAPSHOT 2u              /* 读状态，回 Snapshot。 */
#define HL_CMD_SET_OUTPUTS 3u           /* 写 GPIO，回 Ack。 */
#define HL_CMD_SET_DAC 4u               /* 写 DAC，回 Ack。 */
#define HL_CMD_SET_PWM 5u               /* 写 PWM，回 Ack。 */
#define HL_CMD_SEND_CAN 6u              /* 发 CAN，回 Ack，成功后再报 CanLog。 */

#define HL_ACK_OK 0u                    /* 成功。线上省略这个 0。 */
#define HL_ACK_UNSUPPORTED 1u           /* 板卡做不到，例如 RX 的 CAN FD。 */
#define HL_ACK_BAD_ARG 2u               /* 参数不合法。 */

#define HL_NAME_MAX 16                  /* 板卡名字缓冲，含结尾 0。 */
#define HL_DETAIL_MAX 40                /* Ack 原因缓冲，含结尾 0。 */
#define HL_CAN_MAX 8                    /* CAN 数据最多 8 字节。 */
#define HL_MAX_PAYLOAD 192              /* Envelope 最长字节数。 */
#define HL_MAX_FRAME (10 + HL_MAX_PAYLOAD) /* 2 魔数 + 6 头 + 载荷 + 2 CRC。 */

typedef struct HlEnvelope {
    uint32_t sequence;                 /* 序号。PC 从 1 递增，板卡主动上报从 0x80000000 递增。 */
    uint32_t command;                  /* Command 枚举。 */
    int has_snapshot;                  /* 1 表示本包带状态。 */
    uint32_t tick;                     /* 板卡周期计数。 */
    uint32_t adc0;                     /* 第一路 AD。 */
    uint32_t adc1;                     /* 第二路 AD。 */
    uint32_t inputs;                   /* GPIO 输入，低 4 位。 */
    uint32_t outputs;                  /* GPIO 输出，低 4 位。 */
    uint32_t dac;                      /* DAC 当前码。 */
    uint32_t board;                    /* BoardKind：1 RX71M，2 RA8P。 */
    int can_fd;                        /* 该板是否支持 CAN FD。 */
    char name[HL_NAME_MAX];            /* "RX71M" 或 "RA8P1"。 */
    int has_set_outputs;               /* 1 表示本包带 SetOutputs。 */
    uint32_t outputs_mask;             /* 要写出的 GPIO 掩码，0–15。 */
    int has_set_dac;                   /* 1 表示本包带 SetDac。 */
    uint32_t dac_code;                 /* 要写的 DAC，0–4095。 */
    int has_set_pwm;                   /* 1 表示本包带 SetPwm。 */
    uint32_t pwm_channel;              /* 0–3。 */
    uint32_t pwm_duty_permille;        /* 占空比千分比，0–1000。 */
    int has_send_can;                  /* 1 表示本包带 SendCan。 */
    uint32_t can_channel;              /* 0 或 1。 */
    uint32_t can_id;                   /* 11 位标准帧。 */
    uint8_t can_data[HL_CAN_MAX];      /* 待发送的 CAN 数据。 */
    uint8_t can_dlc;                   /* 1–8。 */
    int can_req_fd;                    /* 1 表示请求 CAN FD。 */
    int has_can_log;                   /* 1 表示本包带 CanLog。 */
    uint32_t log_channel;              /* CanLog 通道。 */
    uint32_t log_id;                   /* CanLog 的 11 位 ID。 */
    uint8_t log_data[HL_CAN_MAX];      /* 已发送的 CAN 数据。 */
    uint8_t log_dlc;                   /* CanLog 数据长度。 */
    int log_fd;                        /* CanLog 是否为 CAN FD。 */
    int log_tx;                        /* 1 表示板卡发出的帧。 */
    int has_ack;                       /* 1 表示本包带 Ack。 */
    uint32_t ack_command;              /* 被应答的命令号。 */
    uint32_t ack_status;               /* 0 成功，1 不支持，2 参数错误。 */
    char ack_detail[HL_DETAIL_MAX];    /* ASCII 原因，成功时为空。 */
} HlEnvelope;

typedef struct HlParser {
    uint8_t state;                     /* 收帧状态机。 */
    uint8_t board;                     /* 帧里的目标板卡号。 */
    uint8_t flags;                     /* 帧标志：bit0 来自板卡，bit1 CAN FD。 */
    uint8_t seq;                       /* 序号低 8 位。 */
    uint16_t len;                      /* 载荷长度。 */
    uint16_t got;                      /* 已收到的载荷字节数。 */
    uint8_t crc_lo;                    /* CRC 低字节，等高字节到齐再比较。 */
    uint8_t prefix[6];                 /* 版本到长度，供 CRC 重算。 */
    uint8_t payload[HL_MAX_PAYLOAD];
} HlParser;

int hl_encode_envelope(const HlEnvelope *msg, uint8_t *out, int cap); /* 返回字节数，失败返回 -1。 */
int hl_decode_envelope(const uint8_t *in, int len, HlEnvelope *msg);  /* 成功返回 0。 */
int hl_encode_frame(uint8_t board, uint8_t flags, uint8_t seq,
                    const uint8_t *payload, uint16_t len,
                    uint8_t *out, int cap); /* 组 HL1 帧，seq 只使用低 8 位。 */
uint16_t hl_crc16(const uint8_t *data, int len); /* CRC-16/CCITT-FALSE，初值 0xFFFF。 */

void hl_parser_init(HlParser *parser);           /* 回到等待 0xA5。 */
int hl_parser_push(HlParser *parser, uint8_t byte); /* 凑齐且 CRC 正确时返回 1。 */

typedef void (*HlWriteFn)(void *ctx, const uint8_t *data, uint16_t len); /* 写出一整帧。 */
typedef void (*HlSetOutputsFn)(void *ctx, uint8_t mask);                  /* 写 GPIO 低 4 位。 */
typedef void (*HlSetDacFn)(void *ctx, uint16_t code);                     /* 写 12 位 DAC。 */
typedef int (*HlSetPwmFn)(void *ctx, uint8_t channel, uint16_t duty_permille); /* 返回 0 成功。 */
typedef int (*HlSendCanFn)(void *ctx, uint8_t channel, uint32_t id,
                           const uint8_t *data, uint8_t dlc, int fd);    /* 返回 0 成功。 */
typedef void (*HlFillFn)(void *ctx, HlEnvelope *msg);                    /* 填写 Snapshot。 */

typedef struct HlBoardOps {
    void *ctx;                         /* 回传给各回调，本工程里未使用。 */
    HlWriteFn write;                   /* 把一整帧写到业务串口。 */
    HlSetOutputsFn set_outputs;
    HlSetDacFn set_dac;
    HlSetPwmFn set_pwm;                /* 返回 0 成功。 */
    HlSendCanFn send_can;              /* 返回 0 成功。 */
    HlFillFn fill_snapshot;            /* 填写 tick、ADC、GPIO、板卡名。 */
} HlBoardOps;

typedef struct HlBoardLink {
    HlParser parser;
    HlBoardOps ops;
    uint8_t board_id;                  /* 本板编号。 */
    uint8_t flags_tx;                  /* 发出帧的标志：来自板卡，RA 另加 CAN FD。 */
    int linked;                        /* 收到过发给本板的合法帧后才为 1。 */
    uint32_t tx_seq;                   /* 下一次主动上报序号。 */
    uint8_t frame_buf[HL_MAX_FRAME];
    HlEnvelope req;                    /* 刚解出的请求。 */
    HlEnvelope rsp;                    /* 准备发出的应答或上报。 */
} HlBoardLink;

void hl_board_init(HlBoardLink *link, uint8_t board_id, int can_fd, const HlBoardOps *ops); /* can_fd 非 0 时发出的帧带 CAN FD 标志。 */
void hl_board_push(HlBoardLink *link, uint8_t byte); /* 业务串口每收到一字节调用一次。 */
void hl_board_send_snapshot(HlBoardLink *link);      /* 未连接时直接返回。 */
void hl_board_send_can_log(HlBoardLink *link, uint8_t channel, uint32_t id,
                           const uint8_t *data, uint8_t dlc, int fd); /* 上报刚发出的 CAN。 */

#ifdef __cplusplus
}
#endif

#endif
