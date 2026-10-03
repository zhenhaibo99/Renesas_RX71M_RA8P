#ifndef BOARD_PRIV_HPP_
#define BOARD_PRIV_HPP_

/* 板级私有接口。按外设分到多个 cpp，函数仍是自由函数，不做成类。
 * 只有 board_app_run() 对 FreeRTOS 公开，声明在 board_resources.hpp。
 */
#include "board_resources.hpp"

#include <cstdint>
#include <cstring>

#include "bsp_api.h"
#include "r_ioport.h"
#include "FreeRTOS.h"
#include "task.h"

#include "../../../protocol/host_link.h"

namespace board
{

/* PCLKA：SCI、SPI、IIC 的时钟，125 MHz。 */
constexpr uint32_t kPclkaHz = 125000000UL;
/* PCLKD：GPT 的计数时钟，250 MHz。 */
constexpr uint32_t kPclkdHz = 250000000UL;
/* 日志口和业务口都是 115200。 */
constexpr uint32_t kUartBaud = 115200UL;
/* LIN 用较低波特率，便于用 GPIO 拉出 Break。 */
constexpr uint32_t kLinBaud = 19200UL;
/* 四路 PWM 的目标频率。 */
constexpr uint32_t kPwmHz = 1000UL;

/* PFS 配成外设功能，而不是普通 GPIO。 */
constexpr uint32_t kPeriph = IOPORT_CFG_PERIPHERAL_PIN;
/* SCI0/2/4/6/8 共用这一组复用号。 */
constexpr uint32_t kSciEven = IOPORT_PERIPHERAL_SCI0_2_4_6_8;
/* SCI1/3/5/7/9 共用这一组复用号。 */
constexpr uint32_t kSciOdd = IOPORT_PERIPHERAL_SCI1_3_5_7_9;

/* 日志文本口：SCI8。 */
extern R_SCI_B0_Type * const kLogUart;
/* 与 PC 通信的业务口：SCI7，走 HL1。 */
extern R_SCI_B0_Type * const kAppUart;
/* LIN：SCI2。 */
extern R_SCI_B0_Type * const kLinUart;

/* 约每 500 ms 加 1，同时当作演示数据。 */
extern uint32_t g_tick;
/* AN001 最近一次采样。 */
extern uint16_t g_adc0;
/* AN007 最近一次采样。 */
extern uint16_t g_adc1;
/* 四路输入，bit0–bit3。 */
extern uint8_t g_inputs;
/* 四路输出的当前掩码。连上 PC 后由上位机接管。 */
extern uint8_t g_outputs;
/* DAC 当前 12 位码。 */
extern uint16_t g_dac;
/* GPT 周期寄存器值，供上位机改占空比时换算。 */
extern uint32_t g_pwm_period;
/* PC 链路。未收到合法帧之前 linked 为 0，不往业务口发二进制。 */
extern HlBoardLink g_link;

bool wait_flag(volatile const uint32_t & reg, uint32_t mask, bool set, uint32_t spins);
bool wait_u8(volatile const uint8_t & reg, uint8_t mask, bool set, uint32_t spins);
void modules_start(void);
void pins_apply(void);
uint8_t sci_brr(uint32_t baud, uint32_t cks);
void sci_open(R_SCI_B0_Type * sci, uint32_t baud, uint32_t cks);
void sci_putc(R_SCI_B0_Type * sci, uint8_t ch);
void sci_write(R_SCI_B0_Type * sci, const char * text);
int sci_getc(R_SCI_B0_Type * sci);
void lin_break_and_sync(void);
void spi_open(void);
uint8_t spi_xfer(uint8_t tx);
void iic_open(void);
bool iic_write(uint8_t addr, uint8_t data);
bool canfd_leave_sleep(R_CANFD_Type * can);
void canfd_open(R_CANFD_Type * can);
bool canfd_send(R_CANFD_Type * can, uint32_t id, const uint8_t * data, uint8_t dlc, bool fd);
void gpt_start(R_GPT0_Type * gpt, uint32_t channel, uint32_t duty, bool both);
int apply_pwm(uint8_t channel, uint16_t duty_permille);
void adc_open(void);
void adc_sample(void);
void dac_open(void);
void dac_write(uint16_t code);
void gpio_write(uint8_t mask);
uint8_t gpio_read(void);
void usb_open(void);
void eth_open(void);
void hex4(char * dst, uint16_t value);
void hex2(char * dst, uint8_t value);
void log_line(void);
extern "C" void ra_write(void * ctx, const uint8_t * data, uint16_t len);
extern "C" void ra_outputs(void * ctx, uint8_t mask);
extern "C" void ra_dac(void * ctx, uint16_t code);
extern "C" int ra_pwm(void * ctx, uint8_t channel, uint16_t duty);
extern "C" int ra_can(void * ctx, uint8_t channel, uint32_t id, const uint8_t * data, uint8_t dlc, int fd);
extern "C" void ra_fill(void * ctx, HlEnvelope * msg);
void link_init(void);
void poll_link(void);
void bringup(void);
void poll(void);

} // namespace board

#endif
