#ifndef SERIAL_PACKETIZER_H
#define SERIAL_PACKETIZER_H

#include <stdint.h>
#include <cstdbool>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_system.h"
#include "esp_log.h"

#include "user_config.h"


template <uart_port_t portNum, uint32_t baud, int rxPin, int txPin, unsigned int maxPayloadLen=16, unsigned int numRxBuffers=2>
class SerialPacketizer
{
public:
    typedef struct {
        int len;
        uint8_t data[maxPayloadLen];
    } BUFFER_T;

private:
    char logTag[14]; // "ser_pkt_uartX"

    typedef enum {
        RX_FSM_STATE_IDLE = 0,
        RX_FSM_STATE_HEADER = 1,
        RX_FSM_STATE_DATA = 2,
        RX_FSM_STATE_FOOTER = 3
    } RX_FSM_STATE_E;

    const char preamble[2] = {0xfe, 0xaa};
    static const int preambleLen = sizeof(preamble) / sizeof(preamble[0]);

    const char postamble[2] = {0x55, 0x01};
    static const int postambleLen = sizeof(postamble) / sizeof(postamble[0]);

    const unsigned int rxDriverQueueSize = 32;
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

        ESP_LOGD(caller->logTag, "Handling task started. Caller: 0x%08x", (uint32_t) params);

        while(1) {
            // TBD: add stop request signal
            activeQueue = xQueueSelectFromSet(caller->procQueueSet, pdMS_TO_TICKS(200));
            if(activeQueue == caller->rxDriverQueue) {
                xQueueReceive(activeQueue, &uart_event, portMAX_DELAY); // no blocking, because select ensures it is available
                if(uart_event.type == UART_DATA) {
                    bool continueProcessing = true;
                    while(continueProcessing) {
                        stat = caller->handleRxData();
                        if(stat < 0) {
                            ESP_LOGW(caller->logTag, "handleRxData returned error code %d", stat);
                            continueProcessing = false;
                        } else if(stat > 0) {
                            if(errQUEUE_FULL == xQueueSendToBack(caller->rxPacketQueue, &(caller->rxBuffer), queueWaitTime)) {
                                ESP_LOGW(caller->logTag, "Received packet couldn't be queued within timeout. Dropping it.");
                            }
                            memset(caller->rxBuffer.data, 0x00, maxPayloadLen);
                            caller->rxBuffer.len = -1;
                        } else {
                            // Status zero means there is nothing more to process currently. 
                            // In consequence this means empty packets will be dropped, which is okay.
                            if(caller->rxBuffer.len == 0) {
                                caller->rxBuffer.len = -1;
                            }
                            continueProcessing = false;
                        }
                    }
                } else if(uart_event.type == UART_BREAK) {
                    // Break events (may) come in when the connected device powers up.
                    // We are not using break signaling for anything at all, so just drop them
                    // with a debug log.
                    ESP_LOGD(caller->logTag, "Unhandled UART break event reveived.");
                } else {
                    ESP_LOGW(caller->logTag, "Unhandled UART event reveived: type = %d", (uint32_t) uart_event.type);
                }
            } else if(activeQueue == caller->txPacketQueue) {
                xQueueReceive(activeQueue, &tmpBuffer, portMAX_DELAY); // no blocking, because select ensures it is available
                stat = caller->handleTxData(tmpBuffer.len, tmpBuffer.data);
                if(0 != stat) {
                    ESP_LOGW(caller->logTag, "handleTxData returned error code %d", stat);
                }
            } else {
                // TBD: implement exit of processing loop
            }
        }

        vTaskDelete(NULL);
    }

    int handleRxData(void)
    {
        int ret = 0;

        size_t charsAvail;
        int readStat;
        uint8_t curChar;

        static RX_FSM_STATE_E state = RX_FSM_STATE_IDLE;
        static int cnt = 0;
        static uint8_t len = 0;
        static bool rxEn = false;

        ESP_ERROR_CHECK(uart_get_buffered_data_len(portNum, &charsAvail));
        while(charsAvail) {
            readStat = uart_read_bytes(portNum, &curChar, 1, 1);
            if(1 != readStat) {
                ESP_LOGW(logTag, "Reading UART byte failed with status %d.", readStat);
                ret = -1;
            } else {
                //ESP_LOGD(logTag, "UART byte received: 0x%02x ('%c').", curChar, curChar);
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
                                ESP_LOGW(logTag, "Invalid preamble byte received.");
                                // TBD: return distinctive error code
                                ret = -1;
                            }
                        } else if(cnt == preambleLen) {
                            // length
                            len = curChar;
                            cnt++;
                        } else {
                            // length inverted
                            if (curChar != (len ^ 0xff)) {
                                cnt = 0;
                                state = RX_FSM_STATE_IDLE;
                                ESP_LOGW(logTag, "Invalid length/inverted-length combo received.");
                                // TBD: return distinctive error code
                                ret = -1;
                            } else {
                                cnt = len;
                                if((len <= maxPayloadLen) && (rxBuffer.len == -1)) {
                                    rxEn = true;
                                    rxBuffer.len = cnt;
                                    memset(rxBuffer.data, 0x00, maxPayloadLen);
                                } else {
                                    rxEn = false;
                                    if(len > maxPayloadLen) {
                                        ESP_LOGW(logTag, "Receiving packet length (%d) is too big. Packet will be dropped.", len);
                                        // TBD: return distinctive error code
                                        ret = -1;
                                    } else {
                                        ESP_LOGE(logTag, "Receive buffer is not free. This can't happen!");
                                        // TBD: return distinctive error code
                                        ret = -1;
                                    }
                                }

                                if(cnt > 0) {
                                    state = RX_FSM_STATE_DATA;
                                } else {
                                    state = RX_FSM_STATE_FOOTER;
                                }
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
                                    // Override charsAvail to break out of the loop; next packet will be processed
                                    // later.
                                    charsAvail = 0;
                                    break;
                                }
                            }
                        } else {
                            cnt = 0;
                            state = RX_FSM_STATE_IDLE;
                            if(rxEn) {
                                rxBuffer.len = -1;
                            }
                            ESP_LOGW(logTag, "Invalid postamble byte received.");
                            // TBD: return distinctive error code
                            ret = -1;
                        }
                        break;

                    default:
                        state = RX_FSM_STATE_IDLE;
                        cnt = 0;
                        ESP_LOGE(logTag, "RX_FSM in invalid state!");
                        // TBD: return distinctive error code
                        ret = -1;
                        break;
                }
            }
        }

        return ret;
    }

    int handleTxData(unsigned int len, uint8_t* data)
    {
        const int retryCntMax = 1;
        unsigned int bytesWritten = 0;
        int stat;
        int retries = retryCntMax + 1;
        static uint8_t txBuffer[maxPayloadLen+preambleLen+postambleLen+2];

        if((len > maxPayloadLen) || (NULL == data)) return -1;

        memcpy(txBuffer, preamble, preambleLen);
        txBuffer[preambleLen] = len & 0xff;
        txBuffer[preambleLen+1] = (len ^ 0xff) & 0xff;
        memcpy(&txBuffer[preambleLen+2], data, len);
        memcpy(&txBuffer[preambleLen+2+len], postamble, postambleLen);

        while((retries > 0) && (bytesWritten < (len+preambleLen+postambleLen+2))) {
            stat = uart_write_bytes(portNum, (char*) &txBuffer[bytesWritten], len+preambleLen+postambleLen+2-bytesWritten);
            if(stat < 0) {
                ESP_ERROR_CHECK(stat);
                retries--;
            } else if (stat == 0) {
                retries--;
            } else {
                bytesWritten += stat;
                retries = retryCntMax + 1;
            }
        }

        if(bytesWritten < (len+preambleLen+postambleLen+2)) {
            return -1;
        } else {
            return 0;
        }
    }

public:
    SerialPacketizer(void)
    {
        snprintf(logTag, sizeof(logTag) / sizeof(logTag[0]), "ser_pkt_uart%d", portNum);

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
        ESP_ERROR_CHECK(uart_driver_install(portNum, uartRxBufferSize, uartTxBufferSize, rxDriverQueueSize, &rxDriverQueue, 0));

        rxPacketQueue = xQueueCreateStatic(numRxBuffers, sizeof(BUFFER_T), rxPacketQueueStorageBuf, &rxPacketQueueBuf);
        txPacketQueue = xQueueCreateStatic(numTxBuffers, sizeof(BUFFER_T), txPacketQueueStorageBuf, &txPacketQueueBuf);

        procQueueSet = xQueueCreateSet(rxDriverQueueSize+numTxBuffers);
        if(pdPASS != xQueueAddToSet(rxDriverQueue, procQueueSet)) {
            ESP_LOGE(logTag, "rxDriverQueue couldn't be added to processing queue set!");
        }
        if(pdPASS != xQueueAddToSet(txPacketQueue, procQueueSet)) {
            ESP_LOGE(logTag, "txPacketQueue couldn't be added to processing queue set!");
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
            ESP_LOGW(logTag, "Transmit packet couldn't be queued within timeout. Dropping it.");
            return -1;
        } else {
            ESP_LOGD(logTag, "Transmit packet queued.");
            return 0;
        }
    }
};

#endif /* SERIAL_PACKETIZER_H */
