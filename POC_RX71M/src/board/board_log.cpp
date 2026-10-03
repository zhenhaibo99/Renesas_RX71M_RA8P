/* RX71M 板级。日志口文本。只走 SCI1，不进业务口。 */
#include "board_priv.hpp"

namespace board
{

/* 高于这个级别的日志丢掉。默认到 INFO，调试行要先把级别放开。 */
uint8_t g_log_level = kLogInfo;


/* 把 16 位数写成 4 个大写十六进制字符。 */
void hex4(char * dst, uint16_t value)
{
    static const char kHex[] = "0123456789ABCDEF";
    dst[0] = kHex[(value >> 12) & 0x0FU];
    dst[1] = kHex[(value >> 8) & 0x0FU];
    dst[2] = kHex[(value >> 4) & 0x0FU];
    dst[3] = kHex[value & 0x0FU];
}


/* 把 8 位数写成 2 个十六进制字符。 */
void hex2(char * dst, uint8_t value)
{
    static const char kHex[] = "0123456789ABCDEF";
    dst[0] = kHex[value >> 4];
    dst[1] = kHex[value & 0x0FU];
}


/* 运行中改过滤级别。只影响之后的 log_write。 */
void log_set_level(uint8_t level)
{
    g_log_level = level;
}


/* 打一行带级别和 tick 的文本。格式：I 00000000 正文。只在应用任务里调用。 */
void log_write(uint8_t level, const char * text)
{
    static const char kMark[] = "EWID";
    char line[80];
    uint32_t pos = 11U;
    if ((text == nullptr) || (text[0] == '\0') || (level > g_log_level))
    {
        return;
    }
    line[0] = (level < 4U) ? kMark[level] : '?'; /* E 故障，W 警告，I 信息，D 调试。 */
    line[1] = ' ';
    hex4(&line[2], static_cast<uint16_t>(g_tick >> 16)); /* tick 高 16 位。 */
    hex4(&line[6], static_cast<uint16_t>(g_tick));       /* tick 低 16 位。 */
    line[10] = ' ';
    while ((*text != '\0') && (pos < 76U)) /* 留出回车换行和结束符。 */
    {
        line[pos] = *text;
        ++text;
        ++pos;
    }
    line[pos] = '\r';
    line[pos + 1U] = '\n';
    line[pos + 2U] = '\0';
    sci_write(kLogUart, line);
}


/* 日志口一行：tick、两路 ADC、输入掩码。不走业务口。 */
void log_line(void)
{
    char line[] = "RX t=0000 adc=0000/0000 in=00\r\n"; /* 占位，下面按固定列覆写。 */
    hex4(&line[5], static_cast<uint16_t>(g_tick)); /* "t=" 后面 4 位。 */
    hex4(&line[14], g_adc0);                       /* AN000。 */
    hex4(&line[19], g_adc1);                       /* AN001。 */
    hex2(&line[27], g_inputs);                     /* "in=" 后面 2 位。 */
    sci_write(kLogUart, line);
}

} // namespace board
