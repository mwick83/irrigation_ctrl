#ifndef IRRIGATION_CONTROLLER_H
#define IRRIGATION_CONTROLLER_H

#include <stdint.h>
#include <cmath> // used for NAN
#include <vector>

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

#define RESERVOIR_STATE_TO_STR(state) (\
    (state == IrrigationController::RESERVOIR_OK) ? "OK" : \
    (state == IrrigationController::RESERVOIR_LOW) ? "LOW" : \
    (state == IrrigationController::RESERVOIR_CRITICAL) ? "CRITICAL" : \
    (state == IrrigationController::RESERVOIR_DISABLED) ? "DISABLED" : \
    "UNKOWN" \
)

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

    typedef enum {
        RESERVOIR_OK = 0,
        RESERVOIR_LOW = 1,
        RESERVOIR_CRITICAL = 2,
        RESERVOIR_DISABLED = 3
    } reservoir_state_t;

    /** Internal state structure used for MQTT updates and persistant storage. */
    typedef struct state_t_ {
        int32_t fillLevel;                                      /**< Fill level of reservoir in percent multiplied by 10.
                                                                 * Note: Will be -1 if getting the fill level failed.
                                                                 */
        reservoir_state_t reservoirState;                       /**< State of the reservoir (e.g. RESERVOIR_OK, ...) */
        uint32_t battVoltage;                                   /**< External battery supply voltage in mV. */
        PowerManager::batt_state_t battState;                   /**< State of the battery (e.g. BATT_FULL, BATT_OK, ...) */
        std::vector<uint32_t> activeOutputs;                    /**< Currently active outputs. */
        time_t nextIrrigEvent;                                  /**< Next time an irrigation event occurs. */
        time_t sntpLastSync;                                    /**< Last time a time sync via SNTP happened. */
        time_t sntpNextSync;                                    /**< Next time a time sync via SNTP should happen. */
    } state_t;

    static const int taskStackSize = 4096;
    static const UBaseType_t taskPrio = tskIDLE_PRIORITY + 5; // TBD
    StackType_t taskStack[taskStackSize];
    StaticTask_t taskBuf;
    TaskHandle_t taskHandle;

    const int wifiConnectedWaitMillis = 16000;              /**< Timeout in milliseconds to wait for WiFi connection */ // TBD: from config
    const int timeResyncWaitMillis = 2000;                  /**< Timeout in milliseconds to wait for an SNTP time resync */ // TBD: from config
    const int mqttConnectedWaitMillis = 3000;               /**< Timeout in milliseconds to wait for a MQTT client connection */ // TBD: from config
    const int mqttAllPublishedWaitMillis = 4000;            /**< Timeout in milliseconds to wait for the MQTT client publishing all messages */ // TBD: from config

    const int wakeupIntervalMillis = 600000;                /**< Nominal wakeup time in milliseconds when going into deep sleep (i.e. non-keepawake) */
    const int wakeupIntervalKeepAwakeMillis = 30000;        /**< Processing task wakeup time in milliseconds when keepawake is active */
    const int noDeepSleepRangeMillis = 60000;               /**< If an event is this close, don't go to deep sleep */

    // In case of deep sleep bare minimum is: peripheralEnStartupMillis + peripheralExtSupplyMillis + wifiConnectedWaitMillis + x
    // This is due to the fact that the lastIrrigEvent time is lost during deep sleep and we need to make sure to reach
    // the initial setup of this variable BEFORE the upcoming event. Otherwise it will be lost.
    // TBD: Nevertheless, storing lastIrrigEvent in RTC memory would be a good option to really make sure no
    // event will be lost.
    /** Time in milliseconds to wakeup before an event */
    const int preEventMillis = peripheralEnStartupMillis + peripheralExtSupplyMillis + 8*100 + 1000; // TBD: 8*100 config options for avg
    /** Time in milliseconds to wakeup before an event in case of deep sleep */
    const int preEventMillisDeepSleep = wifiConnectedWaitMillis + peripheralEnStartupMillis + peripheralExtSupplyMillis + 8*100 + 1000; // TBD: 8*100 config options for avg

    const int noSntpResyncRangeMillis = 60000;              /**< If an event is this close, don't resync time via SNTP */
    const double sntpResyncIntervalHours = 4;               /**< Time in hours after which a time resync via SNTP should be requested */ // TBD: from config
    const double sntpResyncIntervalFailMinutes = 10;        /**< Time in minutes after which a time resync via SNTP should be requested in case it failed previously */ // TBD: from config

    const bool disableReservoirCheck = false;               /**< Can be set to disable the reservoir check when irrigating */ // TBD: from config
    const bool disableBatteryCheck = false;                 /**< Can be set to disable the battery check when irrigating */ // TBD: from config

    state_t state;                                          /**< Internal state representation */
    state_t lastState;                                      /**< Internal state representation (as sent via MQTT the last time) */

    // MQTT related state/data
    bool mqttPrepared = false;
    const char* mqttTopicPre = "whan/irrigation/";          /**< MQTT topic prefix part (i.e. the part before the MAC address) */
    const char* mqttStateTopicPost = "/state";              /**< MQTT topic postfix for state information (i.e. the part after the MAC address) */

    /** MQTT state update data format.
     * Needed format specifiers (in this order!):
     * - \%u Battery voltage in mV,
     * - \%u Battery state (0..4),
     * - \%s Battery state string representation,
     * - \%d Reservoir fill level (multiplied by 10),
     * - \%u Reservoir state (0..3),
     * - \%s Reservoir state string representation,
     * - \%s Next irrigation event occurance ('YYYY-MM-DD HH:MM:SS')
     * - \%s Active outputs array (list of unsigned integers)
     * - \%s Active outputs array (list of names)
     */
    const char* mqttStateDataFmt = "{\n  \"batteryVoltage\": %u,\n  \"batteryState\": %u,\n"
        "  \"batteryStateStr\": \"%s\",\n"
        "  \"reservoirFillLevel\": %d,\n  \"reservoirState\": %u,\n  \"reservoirStateStr\": \"%s\",\n"
        "  \"activeOutputs\": [%s],\n"
        "  \"activeOutputsStr\": [%s],\n"
        "  \"nextIrrigationEvent\": \"%s\",\n"
        "  \"sntpLastSync\": \"%s\",\n"
        "  \"sntpNextSync\": \"%s\"\n"
        "}";
    char* mqttStateTopic;                                   /**< @brief Buffer for the state topic. Will be allocated in constructor and freed in the destructor. */
    char* mqttStateData;                                    /**< @brief Buffer for the state data. Will be allocated in constructor and freed in the destructor. */
    /** Maximum allowed length of the state data.
     * Will be determined by the constructor. Assumption: 5 digits for battery voltage (mV),
     * 1 digit for battery state, 8 digits for battery state string,
     * 4 digits for (fillLevel * 10), 1 digit for reservoir state,
     * 8 digits for reservoir state string,
     * 2 digits per active output + ', ' as seperator,
     * 6 digits per active output string + ', ' as seperator,
     * 19 digits for the next event datetime,
     * 19 digits for the next SNTP sync datetime,
     * 19 digits for the last SNTP sync datetime. */
    size_t mqttStateDataMaxLen;

    static void taskFunc(void* params);
    void setZoneOutputs(bool irrigOk, irrigation_zone_cfg_t* zoneCfg, bool start);
    void updateStateActiveOutputs(uint32_t chNum, bool active);
    void publishStateUpdate(void);

    static void timeSytemEventsHookDispatch(void* param, time_system_event_t events);
    void timeSytemEventHandler(time_system_event_t events);

    EventGroupHandle_t timeEvents;
    const int timeEventTimeSet = (1<<0);
    const int timeEventTimeSetSntp = (1<<1);

public:
    IrrigationController(void);
    ~IrrigationController(void);

    void start(void);
};

#endif /* IRRIGATION_CONTROLLER_H */
