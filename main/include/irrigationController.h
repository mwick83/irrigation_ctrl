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

    /** Internal state structure used for MQTT updates and persistant storage. */
    typedef struct {
        int32_t fillLevel; /**< Fill level of reservoir in percent multiplied by 10.
                            * Note: Will be -1 if getting the fill level failed.
                            */
        uint32_t battVoltage; /**< External battery supply voltage in mV. */
    } state_t;

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

    state_t state; /**< Internal state representation */

    // MQTT related state/data
    bool mqttPrepared = false;
    const char* mqttTopicPre = "whan/irrigation/"; /**< MQTT topic prefix part (i.e. the part before the MAC address) */
    const char* mqttStateTopicPost = "/state"; /**< MQTT topic postfix for state information (i.e. the part after the MAC address) */
    const char* mqttStateDataFmt = "{ \"batteryVoltage\": %u, \"reservoirFillLevel\": %d }";
        /**< MQTT state update data format.
         * Needed format specifiers (in this order!): \%u Battery voltage in mV, \%d Reservoir fill level (multiplied by 10).
         */
    char* mqttStateTopic; /**< @brief Buffer for the state topic. Will be allocated in constructor and freed in the destructor. */
    char* mqttStateData; /**< @brief Buffer for the state data. Will be allocated in constructor and freed in the destructor. */
    size_t mqttStateDataMaxLen; /**< Maximum allowed length of the state data.
                                 * Will be determined by the constructor. Assumption: 5 digits for volatage (mV) and
                                 * 4 digits for (fillLevel * 10) */

    static void taskFunc(void* params);
    void publishStateUpdate(void);

public:
    IrrigationController(void);
    ~IrrigationController(void);

    void start(void);
};

#endif /* IRRIGATION_CONTROLLER_H */
