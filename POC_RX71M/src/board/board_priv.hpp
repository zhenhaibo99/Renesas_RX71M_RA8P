#ifndef BOARD_PRIV_HPP_
#define BOARD_PRIV_HPP_

/* 板级私有接口。按外设分到多个 cpp，函数仍是自由函数，不做成类。
 * 只有 board_app_run() 对 FreeRTOS 公开，声明在 board_resources.hpp。
 */
#include "board_resources.hpp"

#include <cstdint>
#include <cstring>

extern "C"
{
#include "platform.h"
#include "FreeRTOS.h"
#include "task.h"
}

#include "../../../protocol/host_link.h"

namespace board
{

/* SCI、CAN、RSPI、RIIC、MTU 用的外设时钟 PCLKB。 */
constexpr uint32_t kPclkbHz = 60000000UL;
/* 日志口和业务口波特率。 */
constexpr uint32_t kUartBaud = 115200UL;
/* LIN 用较低波特率，便于 GPIO 拉出 Break。 */
constexpr uint32_t kLinBaud = 19200UL;

/* 日志文本：SCI1，TX=P26，RX=P30。 */
extern volatile struct st_sci0 * const kLogUart;
/* 与 PC 的 HL1 业务口：SCI2，TX=P50，RX=P52。 */
extern volatile struct st_sci0 * const kAppUart;
/* LIN：SCI5，TX=PC3，RX=PC2。 */
extern volatile struct st_sci0 * const kLinUart;

/* 约每 500 ms 加 1，同时当作演示数据。 */
extern uint32_t g_tick;
/* AN000 = P40 的最近一次采样。 */
extern uint16_t g_adc0;
/* AN001 = P41 的最近一次采样。 */
extern uint16_t g_adc1;
/* 四路输入，bit0–bit3 = P05、P07、PE0、PE1。 */
extern uint8_t g_inputs;
/* 四路输出掩码，bit0–bit3 = P90–P93。连上 PC 后由上位机接管。 */
extern uint8_t g_outputs;
/* DAC0 当前 12 位码。 */
extern uint16_t g_dac;
/* PC 链路。未收到合法帧前不往业务口发二进制。 */
extern HlBoardLink g_link;

bool wait_true(volatile const unsigned char & reg, unsigned char mask, bool set, uint32_t spins);
void mstp_unlock(void);
void mstp_lock(void);
void modules_start(void);
void periph_pin(volatile unsigned char & pfs, unsigned char sel, volatile unsigned char & pmr, unsigned char bit);
void gpio_dir_out(volatile unsigned char & pmr, volatile unsigned char & pdr, unsigned char bit);
void gpio_dir_in(volatile unsigned char & pmr, volatile unsigned char & pdr, unsigned char bit);
void pins_apply(void);
uint8_t sci_brr(uint32_t baud, uint32_t cks);
void sci_open(volatile struct st_sci0 * sci, uint32_t baud, uint32_t cks);
void sci_putc(volatile struct st_sci0 * sci, uint8_t ch);
void sci_write(volatile struct st_sci0 * sci, const char * text);
int sci_getc(volatile struct st_sci0 * sci);
void lin_break_and_sync(void);
void spi_open(void);
uint8_t spi_xfer(uint8_t tx);
void iic_open(void);
bool iic_write(uint8_t addr, uint8_t data);
bool can_wait_reset(volatile struct st_can & can, bool in_reset);
void can_open(volatile struct st_can & can);
bool can_send(volatile struct st_can & can, uint16_t id, const uint8_t *data, uint8_t dlc);
void pwm_open(void);
uint16_t duty_count(uint16_t duty_permille);
int apply_pwm(uint8_t channel, uint16_t duty_permille);
void adc_sample(void);
void dac_write(uint16_t code);
void gpio_write(uint8_t mask);
uint8_t gpio_read(void);
void usb_open(void);
void eth_open(void);
void hex4(char * dst, uint16_t value);
void hex2(char * dst, uint8_t value);
void log_line(void);
extern "C" void rx_write(void * ctx, const uint8_t * data, uint16_t len);
extern "C" void rx_outputs(void * ctx, uint8_t mask);
extern "C" void rx_dac(void * ctx, uint16_t code);
extern "C" int rx_pwm(void * ctx, uint8_t channel, uint16_t duty);
extern "C" int rx_can(void * ctx, uint8_t channel, uint32_t id, const uint8_t * data, uint8_t dlc, int fd);
extern "C" void rx_fill(void * ctx, HlEnvelope * msg);
void link_init(void);
void poll_link(void);
void bringup(void);
void poll(void);
void iwdt_open(void);    /* 启动独立看门狗，超时约 17.5 s。 */
void iwdt_refresh(void); /* 重装计数。任务每 20 ms 调一次。 */

} // namespace board

#endif
