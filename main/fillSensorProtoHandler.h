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


// TBD: how to encapsulate this correctly?
static const char* LOG_TAG_FILL_PROTO = "fill_proto";

template <class PacketizerClass>
class FillSensorProtoHandler
{
private:
    static const unsigned int maxPacketDataLen = 4;
    
    PacketizerClass* packetizer;
    class PacketizerClass::BUFFER_T rxPacketBuf;
    QueueHandle_t rxPacketQueue;

    bool packetizerInitialized;

    uint8_t txBuffer[maxPacketDataLen+1];

    enum {
        PROTO_TYPE_FILL_LEVEL_REQ = 0x01,
        PROTO_TYPE_FILL_LEVEL_IND = 0x81
    } PROTO_TYPE_E;

public:
    FillSensorProtoHandler(PacketizerClass* packetizer)
    {
        packetizerInitialized = false;
        this->packetizer = packetizer;

        memset(txBuffer, 0x00, maxPacketDataLen+1);

        rxPacketQueue = packetizer->getRxPacketQueue();

        if(NULL == rxPacketQueue) {
            ESP_LOGE(LOG_TAG_FILL_PROTO, "Couldn't get rx packet queue handle from packetizer!");
        } else {
            ESP_LOGD(LOG_TAG_FILL_PROTO, "Packetizer setup ok.");
            packetizerInitialized = true;
        }
    }

    int getFillLevel(void)
    {
        int ret = -1;
        int fillLevel = -1;

        //TickType_t wait = portMAX_DELAY;
        TickType_t wait = pdMS_TO_TICKS(1000);

        if(!packetizerInitialized) return -1;

        txBuffer[0] = PROTO_TYPE_FILL_LEVEL_REQ;

        if(0 == packetizer->transmitData(1, txBuffer, wait)) {
            if(pdPASS == xQueueReceive(rxPacketQueue, &rxPacketBuf, wait)) {
                if((rxPacketBuf.len == 5) && (PROTO_TYPE_FILL_LEVEL_IND == rxPacketBuf.data[0])) {
                    memcpy(&fillLevel, &rxPacketBuf.data[1], 4);
                    ret = fillLevel;
                } else {
                    ESP_LOGE(LOG_TAG_FILL_PROTO, "Received answer isn't a proper fill level indication! len: %d, type: 0x%02x", rxPacketBuf.len, rxPacketBuf.data[0]);
                    // TBD: distinctive error code
                }
            } else {
                ESP_LOGE(LOG_TAG_FILL_PROTO, "Receiving fill level timed out!");
                // TBD: distinctive error code
            }
        } else {
            ESP_LOGE(LOG_TAG_FILL_PROTO, "Couldn't send fill level request.");
            // TBD: distinctive error code
        }

        return ret;
    }
};


#endif /* FILL_SENSOR_PROTO_HANDLER_H */
