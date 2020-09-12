#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H


#include <stdint.h>
#include "driver/uart.h"
#include "driver/adc.h"


static const int heartbeatLedPin = 2;

static const uart_port_t fillSensorPortNum = UART_NUM_1;
static const uint32_t fillSensorPortBaud = 115200;
static const int fillSensorPortRxPin = 21;
static const int fillSensorPortTxPin = 22;

#ifdef __cplusplus
#define FillSensorPacketizer SerialPacketizer<fillSensorPortNum, fillSensorPortBaud, fillSensorPortRxPin, fillSensorPortTxPin, 16, 2>
#endif

static const uart_port_t spareSensorPortNum = UART_NUM_2;
static const uint32_t spareSensorPortBaud = 115200;
static const int spareSensorPortRxPin = 16;
static const int spareSensorPortTxPin = 17;

static const uart_port_t consolePortNum = UART_NUM_0;
#define NO_CONSOLE_IO_LL_INIT
// static const uint32_t consolePortBaud = 115200;
// static const int consolePortRxPin = 16;
// static const int consolePortTxPin = 17;


static const adc1_channel_t battVoltageChannel = ADC1_GPIO35_CHANNEL;

static const gpio_num_t peripheralEnGpioNum = GPIO_NUM_2;
static const gpio_num_t peripheralExtSupplyGpioNum = GPIO_NUM_25;

static const int peripheralEnStartupMillis = 5;         /**< Startup time in milliseconds to wait for onboard 
                                                         * peripherals being ready. Dominated by the DCDC:
                                                         * According to datasheet soft-start is 2.1 ms */
static const int peripheralExtSupplyMillis = 500 ;      /**< Startup time in milliseconds to wait for external
                                                         * peripherals being ready. Fill sensors needs ~400 ms.
                                                         */

static const gpio_num_t irrigationMainGpioNum = GPIO_NUM_4;
static const gpio_num_t irrigationAux0GpioNum = GPIO_NUM_27;
static const gpio_num_t irrigationAux1GpioNum = GPIO_NUM_26;

static const gpio_num_t keepAwakeGpioNum = GPIO_NUM_34;

#endif /* HARDWARE_CONFIG_H */
