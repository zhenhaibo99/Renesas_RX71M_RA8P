/* RX71M 板级。日志口文本。 */
#include "board_priv.hpp"

namespace board
{


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
