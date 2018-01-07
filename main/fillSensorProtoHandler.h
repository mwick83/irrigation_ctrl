#ifndef FILL_SENSOR_PROTO_HANDLER_H
#define FILL_SENSOR_PROTO_HANDLER_H

#include <cstdint>
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
        PROTO_TYPE_FILL_LEVEL_REQ = 0x01,
        PROTO_TYPE_FILL_LEVEL_IND = 0x81
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

    int getFillLevel(void)
    {
        int ret = -1;
        int fillLevel = -1;

        //TickType_t wait = portMAX_DELAY;
        TickType_t wait = pdMS_TO_TICKS(500);

        if(!packetizerInitialized) return -1;

        // TBD: make mutex wait time configurable/as param?
        if(pdTRUE == xSemaphoreTake(requestMutex, portMAX_DELAY)) {
            txBuffer[0] = PROTO_TYPE_FILL_LEVEL_REQ;

            if(0 == packetizer->transmitData(1, txBuffer, wait)) {
                if(pdPASS == xQueueReceive(rxPacketQueue, &rxPacketBuf, wait)) {
                    if((rxPacketBuf.len == 5) && (PROTO_TYPE_FILL_LEVEL_IND == rxPacketBuf.data[0])) {
                        memcpy(&fillLevel, &rxPacketBuf.data[1], 4);
                        ret = fillLevel;
                    } else {
                        ESP_LOGE(logTag, "Received answer isn't a proper fill level indication! len: %d, type: 0x%02x", rxPacketBuf.len, rxPacketBuf.data[0]);
                        // TBD: distinctive error code
                    }
                } else {
                    ESP_LOGE(logTag, "Receiving fill level timed out!");
                    // TBD: distinctive error code
                }
            } else {
                ESP_LOGE(logTag, "Couldn't send fill level request.");
                // TBD: distinctive error code
            }

            if(pdFALSE == xSemaphoreGive(requestMutex)) {
                ESP_LOGE(logTag, "Error occurred releasing the requestMutex.");
                // TBD: distinctive error code
            }
        } else {
            ESP_LOGE(logTag, "Error occurred acquiring the requestMutex.");
            // TBD: distinctive error code
        }

        return ret;
    }
};


#endif /* FILL_SENSOR_PROTO_HANDLER_H */
