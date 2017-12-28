#ifndef SERIAL_PACKETIZER_H
#define SERIAL_PACKETIZER_H

#include <cstdint>
#include <cstdbool>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_system.h"
#include "esp_log.h"

#include "user_config.h"

static const char* LOG_TAG = "ser_pkt"; // TBD: add port number

template <uart_port_t portNum, uint32_t baud, int rxPin, int txPin, unsigned int maxPayloadLen=16, unsigned int numRxBuffers=2>
class SerialPacketizer
{
public:
    typedef struct {
        int len;
        uint8_t data[maxPayloadLen];
    } BUFFER_T;

private:
    typedef enum {
        RX_FSM_STATE_IDLE = 0,
        RX_FSM_STATE_HEADER = 1,
        RX_FSM_STATE_DATA = 2,
        RX_FSM_STATE_FOOTER = 3
    } RX_FSM_STATE_E;

    const char preamble[2] = {0xaa, 0x55};
    static const int preambleLen = sizeof(preamble) / sizeof(preamble[0]);

    const char postamble[2] = {0x55, 0xaa};
    static const int postambleLen = sizeof(postamble) / sizeof(postamble[0]);

    QueueHandle_t rxDriverQueue;

    static const BaseType_t queueWaitTime = pdMS_TO_TICKS(50);

    // rx related state+buffers
    BUFFER_T rxBuffer;
    QueueHandle_t rxPacketQueue;
    uint8_t rxPacketQueueStorageBuf[numRxBuffers*sizeof(BUFFER_T)];
    StaticQueue_t rxPacketQueueBuf;

    // tx related state+buffers
    static const unsigned int numTxBuffers = 2;
    QueueHandle_t txPacketQueue;
    uint8_t txPacketQueueStorageBuf[numTxBuffers*sizeof(BUFFER_T)];
    StaticQueue_t txPacketQueueBuf;

    // processing task state+buffers
    //static const unsigned int taskStackSize = configMINIMAL_STACK_SIZE;
    static const unsigned int taskStackSize = 2048;
    static const UBaseType_t taskPrio = tskIDLE_PRIORITY + 1;
    StackType_t taskStack[taskStackSize];
    StaticTask_t taskBuf;
    TaskHandle_t taskHandle;

    // queue set used for processing
    // defined as member, because it must be set up before anything is queued
    QueueSetHandle_t procQueueSet;

    static void taskFunc(void* params)
    {
        SerialPacketizer<portNum, baud, rxPin, txPin, maxPayloadLen, numRxBuffers>* caller = 
            (SerialPacketizer<portNum, baud, rxPin, txPin, maxPayloadLen, numRxBuffers>*) params;

        QueueSetMemberHandle_t activeQueue;
        int stat;

        static uart_event_t uart_event;
        static BUFFER_T tmpBuffer;

        ESP_LOGD(LOG_TAG, "Handling task started. Caller: 0x%08x", (uint32_t) params);

        while(1) {
            // TBD: add stop request signal
            activeQueue = xQueueSelectFromSet(caller->procQueueSet, pdMS_TO_TICKS(200));
            if(activeQueue == caller->rxDriverQueue) {
                xQueueReceive(activeQueue, &uart_event, portMAX_DELAY); // no blocking, because select ensures it is available
                if(uart_event.type == UART_DATA) {
                    stat = caller->handleRxData();
                    if(stat < 0) {
                        ESP_LOGW(LOG_TAG, "handleRxData returned error code %d", stat);
                    } else {
                        if(errQUEUE_FULL == xQueueSendToBack(caller->rxPacketQueue, &(caller->rxBuffer), queueWaitTime)) {
                            ESP_LOGW(LOG_TAG, "Received packet couldn't be queued within timeout. Dropping it.")
                        }
                        memset(caller->rxBuffer.data, 0x00, maxPayloadLen);
                        caller->rxBuffer.len = -1;
                    }
                } else {
                    ESP_LOGW(LOG_TAG, "Unhandled UART event reveived: type = %d", (uint32_t) uart_event.type);
                }
            } else if(activeQueue == caller->txPacketQueue) {
                xQueueReceive(activeQueue, &tmpBuffer, portMAX_DELAY); // no blocking, because select ensures it is available
                stat = caller->handleTxData(tmpBuffer.len, tmpBuffer.data);
                if(0 != stat) {
                    ESP_LOGW(LOG_TAG, "handleTxData returned error code %d", stat);
                }
            } else {
                // TBD: implement exit of processing loop
            }
        }

        vTaskDelete(NULL);
    }

    int handleRxData(void)
    {
        int ret = -1;

        size_t charsAvail;
        int readStat;
        uint8_t curChar;

        static RX_FSM_STATE_E state = RX_FSM_STATE_IDLE;
        static int cnt = 0;
        static bool rxEn = false;

        ESP_ERROR_CHECK(uart_get_buffered_data_len(portNum, &charsAvail));
        while(charsAvail) {
            readStat = uart_read_bytes(portNum, &curChar, 1, 1);
            if(1 != readStat) {
                ESP_LOGW(LOG_TAG, "Reading UART byte failed with status %d.", readStat);
            } else {
                charsAvail--;

                switch(state) {
                    case RX_FSM_STATE_IDLE:
                        if(curChar == preamble[0]) {
                            state = RX_FSM_STATE_HEADER;
                            cnt = 1;
                        }
                        break;

                    case RX_FSM_STATE_HEADER:
                        if(cnt < preambleLen) {
                            // preamble
                            if(curChar == preamble[cnt]) {
                                cnt++;
                            } else {
                                cnt = 0;
                                state = RX_FSM_STATE_IDLE;
                                ESP_LOGW(LOG_TAG, "Invalid preamble byte received.");
                                // TBD: return distinctive error code
                            }
                        } else {
                            // length
                            cnt = curChar;

                            if((curChar <= maxPayloadLen) && (rxBuffer.len == -1)) {
                                rxEn = true;
                                rxBuffer.len = cnt;
                                memset(rxBuffer.data, 0x00, maxPayloadLen);
                            } else {
                                rxEn = false;
                                if(curChar > maxPayloadLen) {
                                    ESP_LOGW(LOG_TAG, "Receiving packet length (%d) is too big. Packet will be dropped.", curChar);
                                    // TBD: return distinctive error code
                                } else {
                                    ESP_LOGE(LOG_TAG, "Receive buffer is not free. This can't happen!");
                                    // TBD: return distinctive error code
                                }
                            }

                            if(cnt > 0) {
                                state = RX_FSM_STATE_DATA;
                            } else {
                                state = RX_FSM_STATE_FOOTER;
                            }
                        }
                        break;

                    case RX_FSM_STATE_DATA:
                        if(rxEn) {
                            int pos = rxBuffer.len - cnt;
                            rxBuffer.data[pos] = curChar;
                        }

                        cnt--;
                        if(cnt == 0) {
                            state = RX_FSM_STATE_FOOTER;
                        }
                        break;

                    case RX_FSM_STATE_FOOTER:
                        if(curChar == postamble[cnt]) {
                            cnt++;
                            if(cnt == postambleLen) {
                                state = RX_FSM_STATE_IDLE;
                                cnt = 0;
                                if(rxEn) {
                                    ret = rxBuffer.len;
                                    break;
                                }
                            }
                        } else {
                            cnt = 0;
                            state = RX_FSM_STATE_IDLE;
                            if(rxEn) {
                                rxBuffer.len = -1;
                            }
                            ESP_LOGW(LOG_TAG, "Invalid postamble byte received.");
                            // TBD: return distinctive error code
                        }
                        break;

                    default:
                        state = RX_FSM_STATE_IDLE;
                        cnt = 0;
                        ESP_LOGE(LOG_TAG, "RX_FSM in invalid state!");
                        // TBD: return distinctive error code
                        break;
                }
            }
        }

        return ret;
    }

    int handleTxData(unsigned int len, uint8_t* data)
    {
        unsigned int bytesWritten = 0;
        int stat;
        int retries = 1;
        static uint8_t txBuffer[maxPayloadLen+preambleLen+postambleLen+1];

        if((len > maxPayloadLen) || (NULL == data)) return -1;

        memcpy(txBuffer, preamble, preambleLen);
        txBuffer[preambleLen] = len & 0xff;
        memcpy(&txBuffer[preambleLen+1], data, len);
        memcpy(&txBuffer[preambleLen+1+len], postamble, postambleLen);

        while((retries > 0) && (bytesWritten < (len+preambleLen+postambleLen+1))) {
            stat = uart_write_bytes(portNum, (char*) &txBuffer[bytesWritten], len+preambleLen+postambleLen+1-bytesWritten);
            if(stat < 0) {
                ESP_ERROR_CHECK(stat);
            } else {
                bytesWritten += stat;
            }
            retries--;
        }

        if(bytesWritten < (len+preambleLen+postambleLen+1)) {
            return -1;
        } else {
            return 0;
        }
    }

public:
    SerialPacketizer(void)
    {
        const int uartRxBufferSize = (maxPayloadLen*4 < UART_FIFO_LEN*2) ? UART_FIFO_LEN*2 : maxPayloadLen*4;
        const int uartTxBufferSize = (maxPayloadLen*4 < UART_FIFO_LEN*2) ? UART_FIFO_LEN*2 : maxPayloadLen*4;

        rxBuffer.len = -1;
        memset(rxBuffer.data, 0x00, maxPayloadLen);

        uart_config_t cfg;
        cfg.baud_rate = baud;
        cfg.data_bits = UART_DATA_8_BITS;
        cfg.parity = UART_PARITY_DISABLE;
        cfg.stop_bits = UART_STOP_BITS_1;
        cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        cfg.rx_flow_ctrl_thresh = 1;

        ESP_ERROR_CHECK(uart_param_config(portNum, &cfg));
        ESP_ERROR_CHECK(uart_set_pin(portNum, txPin, rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
        ESP_ERROR_CHECK(uart_driver_install(portNum, uartRxBufferSize, uartTxBufferSize, 4, &rxDriverQueue, 0));

        rxPacketQueue = xQueueCreateStatic(numRxBuffers, sizeof(BUFFER_T), rxPacketQueueStorageBuf, &rxPacketQueueBuf);
        txPacketQueue = xQueueCreateStatic(numTxBuffers, sizeof(BUFFER_T), txPacketQueueStorageBuf, &txPacketQueueBuf);

        procQueueSet = xQueueCreateSet(numRxBuffers+numTxBuffers);
        if(pdPASS != xQueueAddToSet(rxDriverQueue, procQueueSet)) {
            ESP_LOGE(LOG_TAG, "rxDriverQueue couldn't be added to processing queue set!")
        }
        if(pdPASS != xQueueAddToSet(txPacketQueue, procQueueSet)) {
            ESP_LOGE(LOG_TAG, "txPacketQueue couldn't be added to processing queue set!")
        }

        //taskHandle = xTaskCreateStatic(this->taskFunc, "serial_packetizer_task", taskStackSize, NULL, taskPrio, taskStack, taskBuf);
        taskHandle = xTaskCreateStatic(taskFunc, "serial_packetizer_task", taskStackSize, (void*) this, taskPrio, taskStack, &taskBuf);
    }

    int getPayloadMax(void) { return maxPayloadLen; }

    QueueHandle_t getRxPacketQueue(void) { return rxPacketQueue; }

    int transmitData(unsigned int len, uint8_t* data, TickType_t wait)
    {
        BaseType_t stat;
        static BUFFER_T tmpBuffer;

        if((len > maxPayloadLen) || (NULL == data)) return -1;

        tmpBuffer.len = len;
        memcpy(tmpBuffer.data, data, len);

        stat = xQueueSendToBack(txPacketQueue, &tmpBuffer, wait);
        if(errQUEUE_FULL == stat) {
            ESP_LOGW(LOG_TAG, "Transmit packet couldn't be queued within timeout. Dropping it.");
            return -1;
        } else {
            ESP_LOGD(LOG_TAG, "Transmit packet queued.");
            return 0;
        }
    }
};

#endif /* SERIAL_PACKETIZER_H */
