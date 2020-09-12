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
        ERR_FILE_IO = -6,
        ERR_NO_RESOURCES = -7
    } err_t;

    typedef struct battery_config_t {
        bool disableBatteryCheck;
        int battCriticalThresholdMilli;
        int battLowThresholdMilli;
        int battOkThresholdMilli;
    } battery_config_t;

    typedef struct reservoir_config_t {
        bool disableReservoirCheck;
        int fillLevelMaxVal;
        int fillLevelMinVal;
        int fillLevelCriticalThresholdPercent10;
        int fillLevelLowThresholdPercent10;
        int fillLevelHysteresisPercent10;
    } reservoir_config_t;

    typedef void(*ConfigUpdatedHookFncPtr)(void*);

    SettingsManager();
    ~SettingsManager();

    void init();

    err_t updateIrrigationConfig(const char* const jsonData, int jsonDataLen, bool noNotify);
    err_t readIrrigationConfigFile();

    err_t updateHardwareConfig(const char* const jsonData, int jsonDataLen, bool noNotify);
    err_t readHardwareConfigFile();

    err_t copyZonesAndEvents(irrigation_zone_cfg_t* zones, IrrigationEvent* events, bool* eventsUsed);
    err_t copyBatteryConfig(battery_config_t* dst);
    err_t copyReservoirConfig(reservoir_config_t* dst);

    err_t registerIrrigConfigUpdatedHook(ConfigUpdatedHookFncPtr hook, void* param);
    err_t registerHardwareConfigUpdatedHook(ConfigUpdatedHookFncPtr hook, void* param);

private:
    const char* logTag = "settings_mgr";

    const TickType_t lockAcquireTimeout = pdMS_TO_TICKS(1000);          /**< Maximum lock acquisition time in OS ticks. */

    SemaphoreHandle_t configMutex;
    StaticSemaphore_t configMutexBuf;

    SemaphoreHandle_t fileIoMutex;
    StaticSemaphore_t fileIoMutexBuf;

    SemaphoreHandle_t hookMutex;
    StaticSemaphore_t hookMutexBuf;

    typedef struct irrigation_config_t {
        irrigation_zone_cfg_t zones[irrigationPlannerNumZones];         /**< Storage holding irrigation zone configurations. */

        IrrigationEvent events[irrigationPlannerNumNormalEvents];       /**< Storage holding irrigation events. */
        bool eventsUsed[irrigationPlannerNumNormalEvents];              /**< Flag weather or not the corresponding event storage is used. */
    } irrigation_config_t;

    irrigation_config_t shadowDataIrrigationConfig;
    battery_config_t shadowDataBatteryConfig;
    reservoir_config_t shadowDataReservoirConfig;

    typedef enum config_file_type_t {
        CONFIG_FILE_IRRIGATION = 0,
        CONFIG_FILE_HARDWARE = 1
    } config_file_type_t;


    static const int numHookTableEntries = 8;
    ConfigUpdatedHookFncPtr irrigConfigUpdatedHooks[numHookTableEntries];
    void* irrigConfigUpdatedHookParamPtrs[numHookTableEntries];
    ConfigUpdatedHookFncPtr hardwareConfigUpdatedHooks[numHookTableEntries];
    void* hardwareConfigUpdatedHookParamPtrs[numHookTableEntries];

    void clearZoneData(irrigation_config_t& settings);
    void clearEventData(irrigation_config_t& settings);
    err_t jsonParseZone(cJSON* zoneJson, irrigation_zone_cfg_t& zoneCfg);
    err_t jsonParseEvent(cJSON* evtJson, IrrigationEvent& evt, bool& used);

    err_t readConfigFile(config_file_type_t type);
    err_t writeConfigFile(const char* const filename, const char* const jsonData, int jsonDataLen);

    void copyBatteryConfigInt(battery_config_t* dst, battery_config_t& src);
    void copyReservoirConfigInt(reservoir_config_t* dst, reservoir_config_t& src);

    void callIrrigConfigUpdatedHooks();
    void callHardwareConfigUpdatedHooks();
};

#endif /* SETTINGS_MANAGER_H */
