#ifndef FILL_SENSOR_PROTO_HANDLER_H
#define FILL_SENSOR_PROTO_HANDLER_H

#include <stdint.h>
#include <cstdbool>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_system.h"
#include "esp_log.h"

#include "user_config.h"

//#include "serialPacketizer.h"


template <class PacketizerClass>
class FillSensorProtoHandler
{
private:
    const char* logTag = "fill_proto";

    static const unsigned int maxPacketDataLen = 4;

    PacketizerClass* packetizer;
    class PacketizerClass::BUFFER_T rxPacketBuf;
    QueueHandle_t rxPacketQueue;

    bool packetizerInitialized;

    uint8_t txBuffer[maxPacketDataLen+1];

    SemaphoreHandle_t requestMutex;
    StaticSemaphore_t requestMutexBuf;

    enum {
        PROTO_TYPE_FILL_LEVEL_REQ       = 0x01,
        PROTO_TYPE_FILL_LEVEL_IND       = 0x81,
        PROTO_TYPE_FILL_LEVEL_RAW_IND   = 0x82
    } PROTO_TYPE_E;

public:
    // This constructor shouldn't be used, but provide minimum init to be safe
    FillSensorProtoHandler(void)
    {
        packetizerInitialized = false;
        this->packetizer = nullptr;

        ESP_LOGE(logTag, "Unsupported default constructor called!");
    }

    FillSensorProtoHandler(PacketizerClass* packetizer)
    {
        packetizerInitialized = false;
        this->packetizer = packetizer;

        memset(txBuffer, 0x00, maxPacketDataLen+1);

        requestMutex = xSemaphoreCreateMutexStatic(&requestMutexBuf);

        rxPacketQueue = packetizer->getRxPacketQueue();

        if(NULL == rxPacketQueue) {
            ESP_LOGE(logTag, "Couldn't get rx packet queue handle from packetizer!");
        } else {
            ESP_LOGD(logTag, "Packetizer setup ok.");
            packetizerInitialized = true;
        }
    }

    int getFillLevel(unsigned int measurements, unsigned int intervalMs)
    {
        int ret = -1;
        unsigned int fillLevelAvg = 0;
        unsigned int measurementsLoop = measurements;

        if(!packetizerInitialized) return -1;

        // TBD: make mutex wait time configurable/as param?
        if(pdTRUE == xSemaphoreTake(requestMutex, portMAX_DELAY)) {
            txBuffer[0] = PROTO_TYPE_FILL_LEVEL_REQ;

            while(measurementsLoop > 0) {
                // Note: The timeout is devided into multiple polls, because the fill sensor
                // may send multiple answer packet (i.e. raw value and the actual percentage)
                TickType_t wait = pdMS_TO_TICKS(100);
                int polls = 5;

                int fillLevel = -1;

                if(0 == packetizer->transmitData(1, txBuffer, wait)) {
                    while(polls >= 0) {
                        if(pdPASS == xQueueReceive(rxPacketQueue, &rxPacketBuf, wait)) {
                            if((rxPacketBuf.len == 5) && (PROTO_TYPE_FILL_LEVEL_IND == rxPacketBuf.data[0])) {
                                int fillLevelRaw = 0;
                                memcpy(&fillLevelRaw, &rxPacketBuf.data[1], 4);

                                if(fillLevel < fillLevelMinVal) fillLevel = fillLevelMinVal;
                                if(fillLevel > fillLevelMaxVal) fillLevel = fillLevelMaxVal;
                                fillLevel = (fillLevelRaw - fillLevelMinVal);
                                fillLevel = fillLevel * 1000 / fillLevelMaxVal;

                                if(fillLevel > 1000) fillLevel = 1000;
                                if(fillLevel < 0) fillLevel = 0;

                                ESP_LOGD(logTag, "Received answer is fill level: %d mm", fillLevelRaw);
                                break;
                            } else if((rxPacketBuf.len == 5) && (PROTO_TYPE_FILL_LEVEL_RAW_IND == rxPacketBuf.data[0])) {
                                uint32_t rawData;
                                memcpy(&rawData, &rxPacketBuf.data[1], 4);
                                ESP_LOGD(logTag, "Received answer is raw fill level. raw: 0x%08x (%d)", rawData, rawData);
                            } else {
                                ESP_LOGE(logTag, "Received answer isn't a proper fill level indication! len: %d, type: 0x%02x", rxPacketBuf.len, rxPacketBuf.data[0]);
                                // TBD: distinctive error code
                                break;
                            }
                        }
                        polls--;
                    }

                    if(polls < 0) {
                        ESP_LOGE(logTag, "Receiving fill level timed out!");
                        // TBD: distinctive error code
                    }
                } else {
                    ESP_LOGE(logTag, "Couldn't send fill level request.");
                    // TBD: distinctive error code
                }

                if(fillLevel >= 0) {
                    fillLevelAvg += fillLevel;
                } else {
                    break;
                }

                measurementsLoop--;
                if(measurementsLoop > 0) {
                    vTaskDelay(pdMS_TO_TICKS(intervalMs));
                }
            }

            if(pdFALSE == xSemaphoreGive(requestMutex)) {
                ESP_LOGE(logTag, "Error occurred releasing the requestMutex.");
                // TBD: distinctive error code
            }
        } else {
            ESP_LOGE(logTag, "Error occurred acquiring the requestMutex.");
            // TBD: distinctive error code
        }

        if(measurementsLoop == 0) {
            ret = fillLevelAvg / measurements;
        }

        return ret;
    }

    int getFillLevel(void) {
        return getFillLevel(1, 0);
    }

};


#endif /* FILL_SENSOR_PROTO_HANDLER_H */
