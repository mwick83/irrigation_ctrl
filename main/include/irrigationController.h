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


class IrrigationController
{
private:
    const char* logTag = "irrig_ctrl";

    static const int taskStackSize = 2048;
    static const UBaseType_t taskPrio = tskIDLE_PRIORITY + 5; // TBD
    StackType_t taskStack[taskStackSize];
    StaticTask_t taskBuf;
    TaskHandle_t taskHandle;

    int timeSetWaitMillis = 10000; // TBD: from config
    int wifiConnecntedWaitMillis = 10000; // TBD: from config

    uint32_t wakeupIntervalMillis = 10000;
    uint64_t wakeupIntervalKeepAwakeMillis = 5000;

    static void taskFunc(void* params);

public:
    IrrigationController(void);

    void start(void);
};

#endif /* IRRIGATION_CONTROLLER_H */
