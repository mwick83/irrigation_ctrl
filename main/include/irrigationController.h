#ifndef IRRIGATION_CONTROLLER_H
#define IRRIGATION_CONTROLLER_H

#include <cstdint>
#include <cmath> // used for NAN

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "user_config.h"

#include "globalComponents.h"
#include "wifiEvents.h"

/**
 * @brief The IrrigationController class is the heart of the software. It performs 
 * data collection, power managmenet of the sensors and the actual
 * decision wether or not to water the plants. It also updates the status information
 * with the gathered data.
 * 
 * Depending on the operation mode (i.e. keep awake jumper is set or otherwise 
 * enforced by software), it will loop within the task and periodically check for 
 * the next watering and perform status updates or it will bring the processor to 
 * deep sleep.
 */
class IrrigationController
{
private:
    const char* logTag = "irrig_ctrl";

    static const int taskStackSize = 2048;
    static const UBaseType_t taskPrio = tskIDLE_PRIORITY + 5; // TBD
    StackType_t taskStack[taskStackSize];
    StaticTask_t taskBuf;
    TaskHandle_t taskHandle;

    int wifiConnectedWaitMillis = 10000; /**< Timeout in milliseconds to wait for WiFi connection */ // TBD: from config
    int timeSetWaitMillis = 10000; /**< Timeout in milliseconds to wait for a valid system time */ // TBD: from config
    int mqttConnectedWaitMillis = 2000; /**< Timeout in milliseconds to wait for a MQTT client connection */ // TBD: from config

    uint32_t wakeupIntervalMillis = 10000; /**< Nominal wakeup time in milliseconds when going into deep sleep (i.e. non-keepawake) */
    uint64_t wakeupIntervalKeepAwakeMillis = 5000; /**< Processing task wakeup time in milliseconds when keepawake is active */

    static void taskFunc(void* params);
    void publishStateUpdate(void);

public:
    IrrigationController(void);

    void start(void);
};

#endif /* IRRIGATION_CONTROLLER_H */
