#include "board_priv.hpp"

/* 跨文件共享的端口指针和运行状态。各外设 cpp 通过 board_priv.hpp 使用。 */
namespace board
{

R_SCI_B0_Type * const kLogUart = R_SCI_B8;
R_SCI_B0_Type * const kAppUart = R_SCI_B7;
R_SCI_B0_Type * const kLinUart = R_SCI_B2;

uint32_t g_tick = 0U;
uint16_t g_adc0 = 0U;
uint16_t g_adc1 = 0U;
uint8_t g_inputs = 0U;
uint8_t g_outputs = 0U;
uint16_t g_dac = 0U;
uint32_t g_pwm_period = 0U;
HlBoardLink g_link;

} // namespace board
