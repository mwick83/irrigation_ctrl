#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H


#include <stdint.h>
#include "driver/uart.h"

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


#endif /* HARDWARE_CONFIG_H */
