#ifndef SETTINGS_MANAGER_H
#define SETTINGS_MANAGER_H

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"

#include "irrigationPlanner.h"
#include "irrigationEvent.h"
#include "irrigationZoneCfg.h"
#include "outputController.h" // needed for CH_MAIN, ...

#include "hardwareConfig.h"

#include "cJSON.h"

/**
 * @brief The SettingsManager class is a manager of all changable settings of the system.
 * It manages storage, reception of new settings and notification of changes.
 */
class SettingsManager
{
public:
    typedef enum err_t {
        ERR_OK = 0,
        ERR_INVALID_ARG = -1,
        ERR_TIMEOUT = -2,
        ERR_INVALID_JSON = -3,
        ERR_SETTINGS_INVALID = -4,
        ERR_PARSING_ERR = -5,
        ERR_FILE_IO = -6
    } err_t;

    SettingsManager();
    ~SettingsManager();

    err_t updateIrrigationConfig(const char* const jsonData, int jsonDataLen);
    err_t readIrrigationConfigFile();

    err_t copyZonesAndEvents(irrigation_zone_cfg_t* zones, IrrigationEvent* events, bool* eventsUsed);

private:
    const char* logTag = "settings_mgr";

    const TickType_t lockAcquireTimeout = pdMS_TO_TICKS(1000);          /**< Maximum lock acquisition time in OS ticks. */

    SemaphoreHandle_t configMutex;
    StaticSemaphore_t configMutexBuf;

    SemaphoreHandle_t fileIoMutex;
    StaticSemaphore_t fileIoMutexBuf;

    typedef struct settings_container_t {
        irrigation_zone_cfg_t zones[irrigationPlannerNumZones];         /**< Storage holding irrigation zone configurations. */

        IrrigationEvent events[irrigationPlannerNumNormalEvents];       /**< Storage holding irrigation events. */
        bool eventsUsed[irrigationPlannerNumNormalEvents];              /**< Flag weather or not the corresponding event storage is used. */
    } settings_container_t;

    settings_container_t shadowData;

    void clearZoneData(settings_container_t& settings);
    void clearEventData(settings_container_t& settings);
    err_t jsonParseZone(cJSON* zoneJson, irrigation_zone_cfg_t& zoneCfg);
    err_t jsonParseEvent(cJSON* evtJson, IrrigationEvent& evt, bool& used);

    err_t writeIrrigationConfigFile(const char* const jsonData, int jsonDataLen);
};

#endif /* SETTINGS_MANAGER_H */
