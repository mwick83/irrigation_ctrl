#ifndef IRRIGATION_CONTROLLER_H
#define IRRIGATION_CONTROLLER_H

#include <stdint.h>
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
        int32_t fillLevel;                                  /**< Fill level of reservoir in percent multiplied by 10.
                                                             * Note: Will be -1 if getting the fill level failed.
                                                             */
        uint32_t battVoltage;                               /**< External battery supply voltage in mV. */
        time_t nextIrrigEvent;                              /**< Next time an irrigation event occurs. */
    } state_t;

    static const int taskStackSize = 2048;
    static const UBaseType_t taskPrio = tskIDLE_PRIORITY + 5; // TBD
    StackType_t taskStack[taskStackSize];
    StaticTask_t taskBuf;
    TaskHandle_t taskHandle;

    const int wifiConnectedWaitMillis = 10000;              /**< Timeout in milliseconds to wait for WiFi connection */ // TBD: from config
    const int timeSetWaitMillis = 10000;                    /**< Timeout in milliseconds to wait for a valid system time */ // TBD: from config
    const int mqttConnectedWaitMillis = 2000;               /**< Timeout in milliseconds to wait for a MQTT client connection */ // TBD: from config
    const int mqttAllPublishedWaitMillis = 2000;            /**< Timeout in milliseconds to wait for the MQTT client publishing all messages */ // TBD: from config

    const uint32_t wakeupIntervalMillis = 10000;            /**< Nominal wakeup time in milliseconds when going into deep sleep (i.e. non-keepawake) */
    const uint32_t wakeupIntervalKeepAwakeMillis = 5000;    /**< Processing task wakeup time in milliseconds when keepawake is active */
    const uint32_t eventComingUpRangeMillis = 60000;        /**< If an event is this close, don't go to deep sleep, but stay in a tight polling loop */
    const uint32_t tightPollMillis = 250;                   /**< Polling interval when in close range to an event */

    state_t state;                                          /**< Internal state representation */

    // MQTT related state/data
    bool mqttPrepared = false;
    const char* mqttTopicPre = "whan/irrigation/";          /**< MQTT topic prefix part (i.e. the part before the MAC address) */
    const char* mqttStateTopicPost = "/state";              /**< MQTT topic postfix for state information (i.e. the part after the MAC address) */

    /** MQTT state update data format.
     * Needed format specifiers (in this order!): 
     * - \%u Battery voltage in mV,
     * - \%d Reservoir fill level (multiplied by 10),
     * - \%s Next irrigation event occurance ('YYYY-MM-DD HH:MM:SS')
     */
    const char* mqttStateDataFmt = "{ \"batteryVoltage\": %u, \"reservoirFillLevel\": %d, \"nextIrrigationEvent\": \"%s\" }";
    char* mqttStateTopic;                                   /**< @brief Buffer for the state topic. Will be allocated in constructor and freed in the destructor. */
    char* mqttStateData;                                    /**< @brief Buffer for the state data. Will be allocated in constructor and freed in the destructor. */
    /** Maximum allowed length of the state data.
     * Will be determined by the constructor. Assumption: 5 digits for volatage (mV),
     * 4 digits for (fillLevel * 10), 19 digits for the next event datetime. */
    size_t mqttStateDataMaxLen;

    static void taskFunc(void* params);
    void publishStateUpdate(void);

public:
    IrrigationController(void);
    ~IrrigationController(void);

    void start(void);
};

#endif /* IRRIGATION_CONTROLLER_H */
