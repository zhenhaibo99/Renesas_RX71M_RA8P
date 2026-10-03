#include "host_link.h"

#include <string.h>

static int pb_need(int pos, int cap, int n)
{
    if ((pos < 0) || (n < 0) || (pos > cap - n))
    {
        return 0;
    }
    return 1;
}

static int pb_u8(uint8_t *out, int cap, int pos, uint8_t v)
{
    if (!pb_need(pos, cap, 1))
    {
        return -1;
    }
    out[pos] = v;
    return pos + 1;
}

static int pb_varint(uint8_t *out, int cap, int pos, uint32_t v)
{
    for (;;)
    {
        uint8_t b = (uint8_t)(v & 0x7Fu);
        v >>= 7;
        if (v != 0u)
        {
            b = (uint8_t)(b | 0x80u);
        }
        pos = pb_u8(out, cap, pos, b);
        if ((pos < 0) || (v == 0u))
        {
            return pos;
        }
    }
}

static int pb_key(uint8_t *out, int cap, int pos, uint32_t field, uint32_t wire)
{
    return pb_varint(out, cap, pos, (field << 3) | wire);
}

static int pb_u32(uint8_t *out, int cap, int pos, uint32_t field, uint32_t v, int force)
{
    if ((v == 0u) && !force)
    {
        return pos;
    }
    pos = pb_key(out, cap, pos, field, 0u);
    if (pos < 0)
    {
        return -1;
    }
    return pb_varint(out, cap, pos, v);
}

static int pb_bool(uint8_t *out, int cap, int pos, uint32_t field, int v, int force)
{
    if (!v && !force)
    {
        return pos;
    }
    pos = pb_key(out, cap, pos, field, 0u);
    if (pos < 0)
    {
        return -1;
    }
    return pb_u8(out, cap, pos, v ? 1u : 0u);
}

static int pb_bytes(uint8_t *out, int cap, int pos, uint32_t field, const uint8_t *data, int len)
{
    if ((data == 0) || (len <= 0))
    {
        return pos;
    }
    pos = pb_key(out, cap, pos, field, 2u);
    if (pos < 0)
    {
        return -1;
    }
    pos = pb_varint(out, cap, pos, (uint32_t)len);
    if ((pos < 0) || !pb_need(pos, cap, len))
    {
        return -1;
    }
    memcpy(out + pos, data, (size_t)len);
    return pos + len;
}

static int pb_str(uint8_t *out, int cap, int pos, uint32_t field, const char *text)
{
    if ((text == 0) || (text[0] == '\0'))
    {
        return pos;
    }
    return pb_bytes(out, cap, pos, field, (const uint8_t *)text, (int)strlen(text));
}

static int pb_sub(uint8_t *out, int cap, int pos, uint32_t field, const uint8_t *body, int len)
{
    if (len < 0)
    {
        return -1;
    }
    pos = pb_key(out, cap, pos, field, 2u);
    if (pos < 0)
    {
        return -1;
    }
    pos = pb_varint(out, cap, pos, (uint32_t)len);
    if (pos < 0)
    {
        return -1;
    }
    if (len == 0)
    {
        return pos;
    }
    if ((body == 0) || !pb_need(pos, cap, len))
    {
        return -1;
    }
    memcpy(out + pos, body, (size_t)len);
    return pos + len;
}

static int enc_snapshot(const HlEnvelope *msg, uint8_t *out, int cap)
{
    int pos = 0;
    pos = pb_u32(out, cap, pos, 1u, msg->tick, 0);
    pos = pb_u32(out, cap, pos, 2u, msg->adc0, 0);
    pos = pb_u32(out, cap, pos, 3u, msg->adc1, 0);
    pos = pb_u32(out, cap, pos, 4u, msg->inputs, 0);
    pos = pb_u32(out, cap, pos, 5u, msg->outputs, 0);
    pos = pb_u32(out, cap, pos, 6u, msg->dac, 0);
    pos = pb_u32(out, cap, pos, 7u, msg->board, 0);
    pos = pb_bool(out, cap, pos, 8u, msg->can_fd, 0);
    pos = pb_str(out, cap, pos, 9u, msg->name);
    return pos;
}

static int enc_can(const HlEnvelope *msg, uint8_t *out, int cap, int log)
{
    int pos = 0;
    uint32_t channel = log ? msg->log_channel : msg->can_channel;
    uint32_t id = log ? msg->log_id : msg->can_id;
    const uint8_t *data = log ? msg->log_data : msg->can_data;
    int dlc = log ? (int)msg->log_dlc : (int)msg->can_dlc;
    int fd = log ? msg->log_fd : msg->can_req_fd;
    pos = pb_u32(out, cap, pos, 1u, channel, 1);
    pos = pb_u32(out, cap, pos, 2u, id, 1);
    pos = pb_bytes(out, cap, pos, 3u, data, dlc);
    pos = pb_bool(out, cap, pos, 4u, fd, 1);
    if (log)
    {
        pos = pb_bool(out, cap, pos, 5u, msg->log_tx, 1);
    }
    return pos;
}

static int enc_ack(const HlEnvelope *msg, uint8_t *out, int cap)
{
    int pos = 0;
    pos = pb_u32(out, cap, pos, 1u, msg->ack_command, 0);
    pos = pb_u32(out, cap, pos, 2u, msg->ack_status, 0);
    pos = pb_str(out, cap, pos, 3u, msg->ack_detail);
    return pos;
}

int hl_encode_envelope(const HlEnvelope *msg, uint8_t *out, int cap)
{
    static uint8_t nested[HL_MAX_PAYLOAD];
    int pos = 0;
    int n;
    if ((msg == 0) || (out == 0) || (cap < 0))
    {
        return -1;
    }
    pos = pb_u32(out, cap, pos, 1u, msg->sequence, 0);
    pos = pb_u32(out, cap, pos, 2u, msg->command, 0);
    if ((pos >= 0) && msg->has_snapshot)
    {
        n = enc_snapshot(msg, nested, (int)sizeof nested);
        pos = pb_sub(out, cap, pos, 3u, nested, n);
    }
    if ((pos >= 0) && msg->has_set_outputs)
    {
        n = pb_u32(nested, (int)sizeof nested, 0, 1u, msg->outputs_mask, 1);
        pos = pb_sub(out, cap, pos, 4u, nested, n);
    }
    if ((pos >= 0) && msg->has_set_dac)
    {
        n = pb_u32(nested, (int)sizeof nested, 0, 1u, msg->dac_code, 1);
        pos = pb_sub(out, cap, pos, 5u, nested, n);
    }
    if ((pos >= 0) && msg->has_set_pwm)
    {
        n = pb_u32(nested, (int)sizeof nested, 0, 1u, msg->pwm_channel, 1);
        if (n >= 0)
        {
            n = pb_u32(nested, (int)sizeof nested, n, 2u, msg->pwm_duty_permille, 1);
        }
        pos = pb_sub(out, cap, pos, 6u, nested, n);
    }
    if ((pos >= 0) && msg->has_send_can)
    {
        n = enc_can(msg, nested, (int)sizeof nested, 0);
        pos = pb_sub(out, cap, pos, 7u, nested, n);
    }
    if ((pos >= 0) && msg->has_can_log)
    {
        n = enc_can(msg, nested, (int)sizeof nested, 1);
        pos = pb_sub(out, cap, pos, 8u, nested, n);
    }
    if ((pos >= 0) && msg->has_ack)
    {
        n = enc_ack(msg, nested, (int)sizeof nested);
        pos = pb_sub(out, cap, pos, 9u, nested, n);
    }
    return pos;
}

typedef struct PbIn {
    const uint8_t *p;
    int n;
    int i;
} PbIn;

static int rd_varint(PbIn *in, uint32_t *out)
{
    uint32_t v = 0u;
    int shift = 0;
    while (shift <= 28)
    {
        uint8_t b;
        if (in->i >= in->n)
        {
            return 0;
        }
        b = in->p[in->i++];
        v |= (uint32_t)(b & 0x7Fu) << shift;
        if ((b & 0x80u) == 0u)
        {
            *out = v;
            return 1;
        }
        shift += 7;
    }
    return 0;
}

static int rd_skip(PbIn *in, uint32_t wire)
{
    uint32_t n = 0u;
    if (wire == 0u)
    {
        return rd_varint(in, &n);
    }
    if (wire == 1u)
    {
        n = 8u;
    }
    else if (wire == 5u)
    {
        n = 4u;
    }
    else if (wire == 2u)
    {
        if (!rd_varint(in, &n))
        {
            return 0;
        }
    }
    else
    {
        return 0;
    }
    if ((in->i > in->n) || ((uint32_t)(in->n - in->i) < n))
    {
        return 0;
    }
    in->i += (int)n;
    return 1;
}

static int rd_exact(PbIn *in, uint8_t *dst, int dst_max, int *out_len)
{
    uint32_t n;
    if (!rd_varint(in, &n))
    {
        return 0;
    }
    if ((in->i > in->n) || ((uint32_t)(in->n - in->i) < n))
    {
        return 0;
    }
    if (n > (uint32_t)dst_max)
    {
        in->i += (int)n;
        return 0;
    }
    if ((n > 0u) && (dst != 0))
    {
        memcpy(dst, in->p + in->i, n);
    }
    in->i += (int)n;
    *out_len = (int)n;
    return 1;
}

static int rd_text(PbIn *in, char *dst, int cap)
{
    uint32_t n;
    int copy;
    if ((dst == 0) || (cap <= 0) || !rd_varint(in, &n))
    {
        return 0;
    }
    if ((in->i > in->n) || ((uint32_t)(in->n - in->i) < n))
    {
        return 0;
    }
    copy = (int)n;
    if (copy >= cap)
    {
        copy = cap - 1;
    }
    if (copy > 0)
    {
        memcpy(dst, in->p + in->i, (size_t)copy);
    }
    dst[copy] = '\0';
    in->i += (int)n;
    return 1;
}

static int take_sub(PbIn *in, PbIn *sub)
{
    uint32_t n;
    if (!rd_varint(in, &n))
    {
        return 0;
    }
    if ((in->i > in->n) || ((uint32_t)(in->n - in->i) < n))
    {
        return 0;
    }
    sub->p = in->p + in->i;
    sub->n = (int)n;
    sub->i = 0;
    in->i += (int)n;
    return 1;
}

static int dec_snapshot(PbIn *in, HlEnvelope *msg)
{
    while (in->i < in->n)
    {
        uint32_t tag;
        uint32_t field;
        uint32_t wire;
        uint32_t v;
        if (!rd_varint(in, &tag))
        {
            return 0;
        }
        field = tag >> 3;
        wire = tag & 7u;
        if ((field == 9u) && (wire == 2u))
        {
            if (!rd_text(in, msg->name, HL_NAME_MAX))
            {
                return 0;
            }
            continue;
        }
        if (wire != 0u)
        {
            if (!rd_skip(in, wire))
            {
                return 0;
            }
            continue;
        }
        if (!rd_varint(in, &v))
        {
            return 0;
        }
        switch (field)
        {
        case 1u: msg->tick = v; break;
        case 2u: msg->adc0 = v; break;
        case 3u: msg->adc1 = v; break;
        case 4u: msg->inputs = v; break;
        case 5u: msg->outputs = v; break;
        case 6u: msg->dac = v; break;
        case 7u: msg->board = v; break;
        case 8u: msg->can_fd = (v != 0u); break;
        default: break;
        }
    }
    return 1;
}

static int dec_can_body(PbIn *in, HlEnvelope *msg, int log)
{
    while (in->i < in->n)
    {
        uint32_t tag;
        uint32_t field;
        uint32_t wire;
        uint32_t v;
        int n = 0;
        uint8_t tmp[HL_CAN_MAX];
        if (!rd_varint(in, &tag))
        {
            return 0;
        }
        field = tag >> 3;
        wire = tag & 7u;
        if ((field == 3u) && (wire == 2u))
        {
            memset(tmp, 0, sizeof tmp);
            if (!rd_exact(in, tmp, HL_CAN_MAX, &n))
            {
                return 0;
            }
            if (log)
            {
                memcpy(msg->log_data, tmp, (size_t)n);
                msg->log_dlc = (uint8_t)n;
            }
            else
            {
                memcpy(msg->can_data, tmp, (size_t)n);
                msg->can_dlc = (uint8_t)n;
            }
            continue;
        }
        if (wire != 0u)
        {
            if (!rd_skip(in, wire))
            {
                return 0;
            }
            continue;
        }
        if (!rd_varint(in, &v))
        {
            return 0;
        }
        if (log)
        {
            switch (field)
            {
            case 1u: msg->log_channel = v; break;
            case 2u: msg->log_id = v; break;
            case 4u: msg->log_fd = (v != 0u); break;
            case 5u: msg->log_tx = (v != 0u); break;
            default: break;
            }
        }
        else
        {
            switch (field)
            {
            case 1u: msg->can_channel = v; break;
            case 2u: msg->can_id = v; break;
            case 4u: msg->can_req_fd = (v != 0u); break;
            default: break;
            }
        }
    }
    return 1;
}

static int dec_ack(PbIn *in, HlEnvelope *msg)
{
    while (in->i < in->n)
    {
        uint32_t tag;
        uint32_t field;
        uint32_t wire;
        uint32_t v;
        if (!rd_varint(in, &tag))
        {
            return 0;
        }
        field = tag >> 3;
        wire = tag & 7u;
        if ((field == 3u) && (wire == 2u))
        {
            if (!rd_text(in, msg->ack_detail, HL_DETAIL_MAX))
            {
                return 0;
            }
            continue;
        }
        if (wire != 0u)
        {
            if (!rd_skip(in, wire))
            {
                return 0;
            }
            continue;
        }
        if (!rd_varint(in, &v))
        {
            return 0;
        }
        if (field == 1u)
        {
            msg->ack_command = v;
        }
        else if (field == 2u)
        {
            msg->ack_status = v;
        }
    }
    return 1;
}

static int dec_two(PbIn *in, uint32_t *a, uint32_t *b)
{
    *a = 0u;
    *b = 0u;
    while (in->i < in->n)
    {
        uint32_t tag;
        uint32_t field;
        uint32_t wire;
        uint32_t v;
        if (!rd_varint(in, &tag))
        {
            return 0;
        }
        field = tag >> 3;
        wire = tag & 7u;
        if (wire != 0u)
        {
            if (!rd_skip(in, wire))
            {
                return 0;
            }
            continue;
        }
        if (!rd_varint(in, &v))
        {
            return 0;
        }
        if (field == 1u)
        {
            *a = v;
        }
        else if (field == 2u)
        {
            *b = v;
        }
    }
    return 1;
}

int hl_decode_envelope(const uint8_t *in_bytes, int len, HlEnvelope *msg)
{
    PbIn in;
    if ((in_bytes == 0) || (msg == 0) || (len < 0))
    {
        return -1;
    }
    memset(msg, 0, sizeof *msg);
    in.p = in_bytes;
    in.n = len;
    in.i = 0;
    while (in.i < in.n)
    {
        uint32_t tag;
        uint32_t field;
        uint32_t wire;
        uint32_t v;
        uint32_t ignore = 0u;
        PbIn sub;
        if (!rd_varint(&in, &tag))
        {
            return -1;
        }
        field = tag >> 3;
        wire = tag & 7u;
        if ((wire == 2u) && (field >= 3u) && (field <= 9u))
        {
            if (!take_sub(&in, &sub))
            {
                return -1;
            }
            if (field == 3u)
            {
                msg->has_snapshot = 1;
                if (!dec_snapshot(&sub, msg))
                {
                    return -1;
                }
            }
            else if (field == 4u)
            {
                msg->has_set_outputs = 1;
                if (!dec_two(&sub, &msg->outputs_mask, &ignore))
                {
                    return -1;
                }
            }
            else if (field == 5u)
            {
                msg->has_set_dac = 1;
                if (!dec_two(&sub, &msg->dac_code, &ignore))
                {
                    return -1;
                }
            }
            else if (field == 6u)
            {
                msg->has_set_pwm = 1;
                if (!dec_two(&sub, &msg->pwm_channel, &msg->pwm_duty_permille))
                {
                    return -1;
                }
            }
            else if (field == 7u)
            {
                msg->has_send_can = 1;
                if (!dec_can_body(&sub, msg, 0))
                {
                    return -1;
                }
            }
            else if (field == 8u)
            {
                msg->has_can_log = 1;
                if (!dec_can_body(&sub, msg, 1))
                {
                    return -1;
                }
            }
            else
            {
                msg->has_ack = 1;
                if (!dec_ack(&sub, msg))
                {
                    return -1;
                }
            }
            continue;
        }
        if (wire != 0u)
        {
            if (!rd_skip(&in, wire))
            {
                return -1;
            }
            continue;
        }
        if (!rd_varint(&in, &v))
        {
            return -1;
        }
        if (field == 1u)
        {
            msg->sequence = v;
        }
        else if (field == 2u)
        {
            msg->command = v;
        }
    }
    return 0;
}

static uint16_t crc_update(uint16_t crc, const uint8_t *data, int len)
{
    int i;
    if (data == 0)
    {
        return crc;
    }
    for (i = 0; i < len; ++i)
    {
        int b;
        unsigned int bits;
        crc = (uint16_t)(crc ^ (uint16_t)((unsigned int)data[i] << 8));
        for (b = 0; b < 8; ++b)
        {
            bits = (unsigned int)crc << 1;
            if ((crc & 0x8000u) != 0u)
            {
                bits ^= 0x1021u;
            }
            crc = (uint16_t)bits;
        }
    }
    return crc;
}

uint16_t hl_crc16(const uint8_t *data, int len)
{
    return crc_update(0xFFFFu, data, len);
}

int hl_encode_frame(uint8_t board, uint8_t flags, uint8_t seq,
                    const uint8_t *payload, uint16_t len,
                    uint8_t *out, int cap)
{
    uint8_t prefix[6];
    uint16_t crc;
    int pos;
    int i;
    if ((out == 0) || (len > HL_MAX_PAYLOAD) || ((payload == 0) && (len != 0u)))
    {
        return -1;
    }
    if (cap < (int)(10u + len))
    {
        return -1;
    }
    prefix[0] = HL_VERSION;
    prefix[1] = board;
    prefix[2] = flags;
    prefix[3] = seq;
    prefix[4] = (uint8_t)(len & 0xFFu);
    prefix[5] = (uint8_t)((len >> 8) & 0xFFu);
    crc = crc_update(0xFFFFu, prefix, 6);
    crc = crc_update(crc, payload, (int)len);
    pos = 0;
    out[pos++] = HL_MAGIC0;
    out[pos++] = HL_MAGIC1;
    for (i = 0; i < 6; ++i)
    {
        out[pos++] = prefix[i];
    }
    if (len > 0u)
    {
        memcpy(out + pos, payload, len);
        pos += (int)len;
    }
    out[pos++] = (uint8_t)(crc & 0xFFu);
    out[pos++] = (uint8_t)((crc >> 8) & 0xFFu);
    return pos;
}

enum
{
    ST_SOF0 = 0,
    ST_SOF1,
    ST_VER,
    ST_BOARD,
    ST_FLAGS,
    ST_SEQ,
    ST_LEN0,
    ST_LEN1,
    ST_PAY,
    ST_CRC0,
    ST_CRC1
};

void hl_parser_init(HlParser *parser)
{
    if (parser != 0)
    {
        memset(parser, 0, sizeof *parser);
    }
}

static void parser_resyn(HlParser *parser, uint8_t byte)
{
    parser->state = (byte == HL_MAGIC0) ? ST_SOF1 : ST_SOF0;
    parser->got = 0u;
}

int hl_parser_push(HlParser *parser, uint8_t byte)
{
    uint16_t crc;
    uint16_t got;
    if (parser == 0)
    {
        return 0;
    }
    switch (parser->state)
    {
    case ST_SOF0:
        parser->state = (byte == HL_MAGIC0) ? ST_SOF1 : ST_SOF0;
        return 0;
    case ST_SOF1:
        parser->state = (byte == HL_MAGIC1) ? ST_VER : ST_SOF0;
        if ((parser->state == ST_SOF0) && (byte == HL_MAGIC0))
        {
            parser->state = ST_SOF1;
        }
        return 0;
    case ST_VER:
        if (byte != HL_VERSION)
        {
            parser_resyn(parser, byte);
            return 0;
        }
        parser->prefix[0] = byte;
        parser->state = ST_BOARD;
        return 0;
    case ST_BOARD:
        parser->board = byte;
        parser->prefix[1] = byte;
        parser->state = ST_FLAGS;
        return 0;
    case ST_FLAGS:
        parser->flags = byte;
        parser->prefix[2] = byte;
        parser->state = ST_SEQ;
        return 0;
    case ST_SEQ:
        parser->seq = byte;
        parser->prefix[3] = byte;
        parser->state = ST_LEN0;
        return 0;
    case ST_LEN0:
        parser->len = byte;
        parser->prefix[4] = byte;
        parser->state = ST_LEN1;
        return 0;
    case ST_LEN1:
        parser->len = (uint16_t)(parser->len | ((uint16_t)byte << 8));
        parser->prefix[5] = byte;
        parser->got = 0u;
        if (parser->len > HL_MAX_PAYLOAD)
        {
            parser_resyn(parser, byte);
            return 0;
        }
        parser->state = (parser->len == 0u) ? ST_CRC0 : ST_PAY;
        return 0;
    case ST_PAY:
        parser->payload[parser->got++] = byte;
        if (parser->got >= parser->len)
        {
            parser->state = ST_CRC0;
        }
        return 0;
    case ST_CRC0:
        parser->crc_lo = byte;
        parser->state = ST_CRC1;
        return 0;
    case ST_CRC1:
        got = (uint16_t)(parser->crc_lo | ((uint16_t)byte << 8));
        crc = crc_update(0xFFFFu, parser->prefix, 6);
        crc = crc_update(crc, parser->payload, (int)parser->len);
        parser->state = ST_SOF0;
        parser->got = 0u;
        return got == crc;
    default:
        parser->state = ST_SOF0;
        return 0;
    }
}

static void copy_text(char *dst, int cap, const char *src)
{
    int i = 0;
    if (cap <= 0)
    {
        return;
    }
    if (src == 0)
    {
        src = "";
    }
    while ((src[i] != '\0') && (i + 1 < cap))
    {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static int board_send(HlBoardLink *link, const HlEnvelope *msg)
{
    static uint8_t payload[HL_MAX_PAYLOAD];
    int n;
    int framed;
    if ((link == 0) || (link->ops.write == 0) || (msg == 0))
    {
        return -1;
    }
    n = hl_encode_envelope(msg, payload, HL_MAX_PAYLOAD);
    if (n < 0)
    {
        return -1;
    }
    framed = hl_encode_frame(link->board_id, link->flags_tx, (uint8_t)(msg->sequence & 0xFFu),
                             payload, (uint16_t)n, link->frame_buf, HL_MAX_FRAME);
    if (framed < 0)
    {
        return -1;
    }
    link->ops.write(link->ops.ctx, link->frame_buf, (uint16_t)framed);
    return 0;
}

static void fill_ack(HlEnvelope *rsp, uint32_t command, uint32_t status, const char *detail)
{
    rsp->has_ack = 1;
    rsp->ack_command = command;
    rsp->ack_status = status;
    copy_text(rsp->ack_detail, HL_DETAIL_MAX, detail);
}

static void board_on_frame(HlBoardLink *link)
{
    HlEnvelope *req = &link->req;
    HlEnvelope *rsp = &link->rsp;
    int rc;
    if ((link->parser.board != HL_BOARD_ANY) && (link->parser.board != link->board_id))
    {
        return;
    }
    link->linked = 1;
    memset(rsp, 0, sizeof *rsp);
    if (hl_decode_envelope(link->parser.payload, (int)link->parser.len, req) != 0)
    {
        rsp->sequence = link->parser.seq;
        fill_ack(rsp, 0u, HL_ACK_BAD_ARG, "bad protobuf");
        board_send(link, rsp);
        return;
    }
    rsp->sequence = req->sequence != 0u ? req->sequence : link->parser.seq;
    rsp->command = req->command;
    switch (req->command)
    {
    case HL_CMD_PING:
    case HL_CMD_SNAPSHOT:
        rsp->has_snapshot = 1;
        if (link->ops.fill_snapshot != 0)
        {
            link->ops.fill_snapshot(link->ops.ctx, rsp);
        }
        break;
    case HL_CMD_SET_OUTPUTS:
        if (!req->has_set_outputs || (req->outputs_mask > 15u))
        {
            fill_ack(rsp, req->command, HL_ACK_BAD_ARG, "bad outputs");
        }
        else
        {
            if (link->ops.set_outputs != 0)
            {
                link->ops.set_outputs(link->ops.ctx, (uint8_t)req->outputs_mask);
            }
            fill_ack(rsp, req->command, HL_ACK_OK, "");
        }
        break;
    case HL_CMD_SET_DAC:
        if (!req->has_set_dac || (req->dac_code > 4095u))
        {
            fill_ack(rsp, req->command, HL_ACK_BAD_ARG, "bad dac");
        }
        else
        {
            if (link->ops.set_dac != 0)
            {
                link->ops.set_dac(link->ops.ctx, (uint16_t)req->dac_code);
            }
            fill_ack(rsp, req->command, HL_ACK_OK, "");
        }
        break;
    case HL_CMD_SET_PWM:
        if (!req->has_set_pwm || (req->pwm_channel > 3u) || (req->pwm_duty_permille > 1000u))
        {
            fill_ack(rsp, req->command, HL_ACK_BAD_ARG, "bad pwm");
        }
        else if ((link->ops.set_pwm == 0) ||
                 (link->ops.set_pwm(link->ops.ctx, (uint8_t)req->pwm_channel,
                                    (uint16_t)req->pwm_duty_permille) != 0))
        {
            fill_ack(rsp, req->command, HL_ACK_BAD_ARG, "bad pwm");
        }
        else
        {
            fill_ack(rsp, req->command, HL_ACK_OK, "");
        }
        break;
    case HL_CMD_SEND_CAN:
        if (!req->has_send_can || (req->can_channel > 1u) || (req->can_dlc == 0u) ||
            (req->can_dlc > HL_CAN_MAX) || (req->can_id > 0x7FFu))
        {
            fill_ack(rsp, req->command, HL_ACK_BAD_ARG, "bad can");
        }
        else if (req->can_req_fd && ((link->flags_tx & HL_FLAG_CAN_FD) == 0u))
        {
            fill_ack(rsp, req->command, HL_ACK_UNSUPPORTED, "can fd unsupported");
        }
        else
        {
            rc = (link->ops.send_can == 0) ? -1 :
                 link->ops.send_can(link->ops.ctx, (uint8_t)req->can_channel, req->can_id,
                                    req->can_data, req->can_dlc, req->can_req_fd);
            if (rc != 0)
            {
                fill_ack(rsp, req->command, HL_ACK_BAD_ARG, "can busy");
            }
            else
            {
                fill_ack(rsp, req->command, HL_ACK_OK, "");
                board_send(link, rsp);
                hl_board_send_can_log(link, (uint8_t)req->can_channel, req->can_id,
                                      req->can_data, req->can_dlc, req->can_req_fd);
                return;
            }
        }
        break;
    default:
        fill_ack(rsp, req->command, HL_ACK_UNSUPPORTED, "unknown command");
        break;
    }
    board_send(link, rsp);
}

void hl_board_init(HlBoardLink *link, uint8_t board_id, int can_fd, const HlBoardOps *ops)
{
    if (link == 0)
    {
        return;
    }
    memset(link, 0, sizeof *link);
    if (ops != 0)
    {
        link->ops = *ops;
    }
    link->board_id = board_id;
    link->flags_tx = HL_FLAG_FROM_BOARD;
    if (can_fd)
    {
        link->flags_tx = (uint8_t)(link->flags_tx | HL_FLAG_CAN_FD);
    }
    link->tx_seq = 0x80000000u;
    hl_parser_init(&link->parser);
}

void hl_board_push(HlBoardLink *link, uint8_t byte)
{
    if ((link != 0) && hl_parser_push(&link->parser, byte))
    {
        board_on_frame(link);
    }
}

static uint32_t next_tx_seq(HlBoardLink *link)
{
    uint32_t seq = link->tx_seq++;
    if (link->tx_seq < 0x80000000u)
    {
        link->tx_seq = 0x80000000u;
    }
    return seq;
}

void hl_board_send_snapshot(HlBoardLink *link)
{
    if ((link == 0) || !link->linked)
    {
        return;
    }
    memset(&link->rsp, 0, sizeof link->rsp);
    link->rsp.sequence = next_tx_seq(link);
    link->rsp.command = HL_CMD_SNAPSHOT;
    link->rsp.has_snapshot = 1;
    if (link->ops.fill_snapshot != 0)
    {
        link->ops.fill_snapshot(link->ops.ctx, &link->rsp);
    }
    board_send(link, &link->rsp);
}

void hl_board_send_can_log(HlBoardLink *link, uint8_t channel, uint32_t id,
                           const uint8_t *data, uint8_t dlc, int fd)
{
    if ((link == 0) || !link->linked || (data == 0) || (dlc == 0u) || (dlc > HL_CAN_MAX))
    {
        return;
    }
    memset(&link->rsp, 0, sizeof link->rsp);
    link->rsp.sequence = next_tx_seq(link);
    link->rsp.has_can_log = 1;
    link->rsp.log_channel = channel;
    link->rsp.log_id = id;
    memcpy(link->rsp.log_data, data, dlc);
    link->rsp.log_dlc = dlc;
    link->rsp.log_fd = fd ? 1 : 0;
    link->rsp.log_tx = 1;
    board_send(link, &link->rsp);
}
