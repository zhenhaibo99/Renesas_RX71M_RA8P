#include "board_priv.hpp"

/* 跨文件共享的端口指针和运行状态。各外设 cpp 通过 board_priv.hpp 使用。 */
namespace board
{

volatile struct st_sci0 * const kLogUart = &SCI1;
volatile struct st_sci0 * const kAppUart = &SCI2;
volatile struct st_sci0 * const kLinUart = &SCI5;

uint32_t g_tick = 0U;
uint16_t g_adc0 = 0U;
uint16_t g_adc1 = 0U;
uint8_t g_inputs = 0U;
uint8_t g_outputs = 0U;
uint16_t g_dac = 0U;
HlBoardLink g_link;

} // namespace board
