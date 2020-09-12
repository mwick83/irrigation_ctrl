#include "settingsManager.h"

#include <stdio.h>

#include "globalComponents.h"
#include "irrigationController.h"
extern IrrigationController irrigCtrl;

extern const uint8_t irrigationConfig_default_json_start[] asm("_binary_irrigationConfig_default_json_start");
extern const uint8_t irrigationConfig_default_json_end[] asm("_binary_irrigationConfig_default_json_end");
extern const uint8_t hardwareConfig_default_json_start[] asm("_binary_hardwareConfig_default_json_start");
extern const uint8_t hardwareConfig_default_json_end[] asm("_binary_hardwareConfig_default_json_end");

/**
 * @brief Default constructor, which performs basic initialization.
 */
SettingsManager::SettingsManager()
{
    // Clear event and zone shadow storages
    clearZoneData(shadowDataIrrigationConfig);
    clearEventData(shadowDataIrrigationConfig);

    configMutex = xSemaphoreCreateMutexStatic(&configMutexBuf);
    fileIoMutex = xSemaphoreCreateMutexStatic(&fileIoMutexBuf);
    hookMutex = xSemaphoreCreateMutexStatic(&hookMutexBuf);

    for (int i=0; i<numHookTableEntries; i++) {
        irrigConfigUpdatedHooks[i] = nullptr;
        irrigConfigUpdatedHookParamPtrs[i] = nullptr;
        hardwareConfigUpdatedHooks[i] = nullptr;
        hardwareConfigUpdatedHookParamPtrs[i] = nullptr;
    }
}

/**
 * @brief Default destructor, which cleans up allocated data.
 */
SettingsManager::~SettingsManager()
{
    if (configMutex) vSemaphoreDelete(configMutex);
    if (fileIoMutex) vSemaphoreDelete(fileIoMutex);
    if (hookMutex) vSemaphoreDelete(hookMutex);
}

void SettingsManager::init()
{
    // setup default data so defaults are available as soon as possible
    ESP_LOGD(logTag, "Loading default configuration.");
    updateIrrigationConfig((const char*) irrigationConfig_default_json_start, irrigationConfig_default_json_end - irrigationConfig_default_json_start + 1, true);
    updateHardwareConfig((const char*) hardwareConfig_default_json_start, hardwareConfig_default_json_end - hardwareConfig_default_json_start + 1, true);
}

void SettingsManager::clearZoneData(irrigation_config_t& settings)
{
    for(int i = 0; i < irrigationPlannerNumZones; i++) {
        settings.zones[i].name[irrigationZoneCfgNameLen] = '\0';
        for(int j = 0; j < irrigationZoneCfgElements; j++) {
            settings.zones[i].chEnabled[j] = false;
        }
    }
}

void SettingsManager::clearEventData(irrigation_config_t& settings)
{
    for(int i = 0; i < irrigationPlannerNumNormalEvents; i++) {
        settings.eventsUsed[i] = false;
    }
}

SettingsManager::err_t SettingsManager::jsonParseZone(cJSON* zoneJson, irrigation_zone_cfg_t& zoneCfg)
{
    err_t ret = ERR_OK;

    cJSON* namePtr = cJSON_GetObjectItem(zoneJson, "name");
    cJSON* chEnPtr = cJSON_GetObjectItem(zoneJson, "chEnabled");
    cJSON* chNumPtr = cJSON_GetObjectItem(zoneJson, "chNum");
    cJSON* chStateStartPtr = cJSON_GetObjectItem(zoneJson, "chStateStart");
    cJSON* chStateStopPtr = cJSON_GetObjectItem(zoneJson, "chStateStop");
    cJSON* elem;
    int numZones;

    if( (nullptr != namePtr) && (nullptr != chEnPtr) && (nullptr != chNumPtr) &&
        (nullptr != chStateStartPtr) && (nullptr != chStateStopPtr) )
    {
        if( cJSON_IsString(namePtr) && cJSON_IsArray(chEnPtr) && cJSON_IsArray(chNumPtr) &&
            cJSON_IsArray(chStateStartPtr) && cJSON_IsArray(chStateStopPtr) &&
            ((numZones = cJSON_GetArraySize(chEnPtr)) <= irrigationZoneCfgElements) &&
            (numZones == cJSON_GetArraySize(chNumPtr)) && (numZones == cJSON_GetArraySize(chStateStartPtr)) &&
            (numZones == cJSON_GetArraySize(chStateStopPtr)) )
        {
            strncpy(zoneCfg.name, cJSON_GetStringValue(namePtr), irrigationZoneCfgNameLen);
            zoneCfg.name[irrigationZoneCfgNameLen] = '\0';

            int i = 0;
            cJSON_ArrayForEach(elem, chEnPtr) {
                if(!cJSON_IsBool(elem)) {
                    ret = ERR_PARSING_ERR;
                    break;
                }
                zoneCfg.chEnabled[i] = cJSON_IsTrue(elem);
                i++;
            }

            i = 0;
            cJSON_ArrayForEach(elem, chNumPtr) {
                if(!cJSON_IsNumber(elem)) {
                    ret = ERR_PARSING_ERR;
                    break;
                }
                zoneCfg.chNum[i] = (OutputController::ch_map_t) elem->valueint;
                i++;
            }

            i = 0;
            cJSON_ArrayForEach(elem, chStateStartPtr) {
                if(!cJSON_IsBool(elem)) {
                    ret = ERR_PARSING_ERR;
                    break;
                }
                zoneCfg.chStateStart[i] = cJSON_IsTrue(elem);
                i++;
            }

            i = 0;
            cJSON_ArrayForEach(elem, chStateStopPtr) {
                if(!cJSON_IsBool(elem)) {
                    ret = ERR_PARSING_ERR;
                    break;
                }
                zoneCfg.chStateStop[i] = cJSON_IsTrue(elem);
                i++;
            }
        } else {
            ret = ERR_PARSING_ERR;
        }
    } else {
        ret = ERR_PARSING_ERR;
    }

    return ret;
}

SettingsManager::err_t SettingsManager::jsonParseEvent(cJSON* evtJson, IrrigationEvent& evt, bool& used)
{
    err_t ret = ERR_OK;

    cJSON* zoneNumPtr = cJSON_GetObjectItem(evtJson, "zoneNum");
    cJSON* durationSecsPtr = cJSON_GetObjectItem(evtJson, "durationSecs");
    cJSON* isSinglePtr = cJSON_GetObjectItem(evtJson, "isSingle");
    cJSON* isDailyPtr = cJSON_GetObjectItem(evtJson, "isDaily");
    cJSON* hourPtr = cJSON_GetObjectItem(evtJson, "hour");
    cJSON* minutePtr = cJSON_GetObjectItem(evtJson, "minute");
    cJSON* secondPtr = cJSON_GetObjectItem(evtJson, "second");
    cJSON* dayPtr = cJSON_GetObjectItem(evtJson, "day");
    cJSON* monthPtr = cJSON_GetObjectItem(evtJson, "month");
    cJSON* yearPtr = cJSON_GetObjectItem(evtJson, "year");

    if( (nullptr != zoneNumPtr) && (nullptr != durationSecsPtr) && (nullptr != hourPtr) &&
        (nullptr != minutePtr) && (nullptr != secondPtr) &&
        cJSON_IsNumber(zoneNumPtr) && cJSON_IsNumber(durationSecsPtr) && cJSON_IsNumber(hourPtr) &&
        cJSON_IsNumber(minutePtr) && cJSON_IsNumber(secondPtr) )
    {
        if( (nullptr != isSinglePtr) && cJSON_IsBool(isSinglePtr) && cJSON_IsTrue(isSinglePtr) &&
            (nullptr != dayPtr) && (nullptr != monthPtr) && (nullptr != yearPtr) &&
            cJSON_IsNumber(dayPtr) && cJSON_IsNumber(monthPtr) && cJSON_IsNumber(yearPtr))
        {
            if(IrrigationEvent::ERR_OK != evt.setSingleEvent(
                hourPtr->valueint, minutePtr->valueint, secondPtr->valueint,
                dayPtr->valueint, monthPtr->valueint, yearPtr->valueint))
            {
                ret = ERR_PARSING_ERR;
            }
            if(IrrigationEvent::ERR_OK != evt.setZoneIndex(zoneNumPtr->valueint)) {
                ret = ERR_PARSING_ERR;
            }
            evt.setDuration(durationSecsPtr->valueint);
            evt.setStartFlag(true);
            used = true;
        } else if((nullptr != isDailyPtr) && cJSON_IsBool(isDailyPtr) && cJSON_IsTrue(isDailyPtr)) {
            if(IrrigationEvent::ERR_OK != evt.setDailyRepetition(
                hourPtr->valueint, minutePtr->valueint, secondPtr->valueint))
            {
                ret = ERR_PARSING_ERR;
            }
            if(IrrigationEvent::ERR_OK != evt.setZoneIndex(zoneNumPtr->valueint)) {
                ret = ERR_PARSING_ERR;
            }
            evt.setDuration(durationSecsPtr->valueint);
            evt.setStartFlag(true);
            used = true;
        } else {
            ret = ERR_PARSING_ERR;
        }
    }
    return ret;
}

SettingsManager::err_t SettingsManager::updateIrrigationConfig(const char* const jsonData, int jsonDataLen, bool noNotify)
{
    err_t ret = ERR_OK;
    static char jsonStr[8192]; // data is not a NULL-terminated string, therefore we need to pre-process it

    if (nullptr == jsonData) return ERR_INVALID_ARG;
    if (jsonDataLen < 2) return ERR_INVALID_ARG; // check for minimum data length ("{}")
    if (jsonDataLen > (sizeof(jsonStr) - 1)) return ERR_INVALID_ARG; // chekc for maximum buffer space

    if (pdFALSE == xSemaphoreTake(configMutex, lockAcquireTimeout)) {
        ESP_LOGE(logTag, "Couldn't acquire config lock within timeout!");
        ret = ERR_TIMEOUT;
    } else {
        ESP_LOGI(logTag, "Parsing irrigation config update.");

        static irrigation_config_t settingsTemp;

        pwrMgr.setKeepAwakeForce(true);

        clearZoneData(settingsTemp);
        clearEventData(settingsTemp);

        memcpy(jsonStr, jsonData, sizeof(char) * jsonDataLen);
        jsonStr[jsonDataLen] = 0;

        cJSON* root = cJSON_ParseWithOpts(jsonStr, nullptr, true);
        cJSON* zones;
        cJSON* events;
        if(nullptr != root) {
            zones = cJSON_GetObjectItem(root, "zones");
            events = cJSON_GetObjectItem(root, "events");

            int numZones;
            int numEvents;

            bool parsingErr = false;
            if( (nullptr != zones) && (nullptr != events) &&
                cJSON_IsArray(zones) && ((numZones = cJSON_GetArraySize(zones)) <= irrigationPlannerNumZones) &&
                cJSON_IsArray(events) && ((numEvents = cJSON_GetArraySize(events)) <= irrigationPlannerNumNormalEvents) )
            {
                int i = 0;
                cJSON* elem;
                cJSON_ArrayForEach(elem, zones) {
                    ESP_LOGD(logTag, "Parsing zone %d", i);
                    if(ERR_OK != jsonParseZone(elem, settingsTemp.zones[i])) {
                        parsingErr = true;
                        break;
                    }
                    i++;
                }

                i = 0;
                cJSON_ArrayForEach(elem, events) {
                    ESP_LOGD(logTag, "Parsing event %d", i);
                    if(ERR_OK != jsonParseEvent(elem, settingsTemp.events[i], settingsTemp.eventsUsed[i])) {
                        parsingErr = true;
                        break;
                    }
                    i++;
                }
            } else {
                ESP_LOGE(logTag, "Zone or event config not found in JSON or have wrong type / length!");
                ret = ERR_SETTINGS_INVALID;
            }

            if(parsingErr) {
                ESP_LOGE(logTag, "Parsing zone or event config from JSON failed!");
                ret = ERR_SETTINGS_INVALID;
            }
        } else {
            ESP_LOGE(logTag, "Parsing JSON tree failed!");
            ret = ERR_INVALID_JSON;
        }

        if(ret == ERR_OK) {
            ESP_LOGI(logTag, "Zone and event data successfully parsed.");
            memcpy(shadowDataIrrigationConfig.zones, settingsTemp.zones, sizeof(irrigation_zone_cfg_t) * irrigationZoneCfgElements);
            for(int i = 0; i < irrigationPlannerNumNormalEvents; i++) {
                shadowDataIrrigationConfig.events[i] = settingsTemp.events[i];
                shadowDataIrrigationConfig.eventsUsed[i] = settingsTemp.eventsUsed[i];
            }
        }

        cJSON* storePersistentPtr = cJSON_GetObjectItem(root, "storePersistent");
        if( (nullptr != storePersistentPtr) && cJSON_IsBool(storePersistentPtr) && cJSON_IsTrue(storePersistentPtr) ) {
            ESP_LOGI(logTag, "Persistent storage of irrigation config requested.");

            cJSON_DetachItemViaPointer(root, storePersistentPtr);

            char* jsonStrModified = cJSON_Print(root);
            int jsonStrModifiedLen = strlen(jsonStrModified);

            if (ERR_OK != writeConfigFile(filenameIrrigationConfig, jsonStrModified, jsonStrModifiedLen)) {
                ret = ERR_FILE_IO;
            }
        }

        xSemaphoreGive(configMutex);

        if((ret == ERR_OK) && !noNotify) {
            callIrrigConfigUpdatedHooks();
        }

        pwrMgr.setKeepAwakeForce(false);
    }

    return ret;
}

SettingsManager::err_t SettingsManager::updateHardwareConfig(const char* const jsonData, int jsonDataLen, bool noNotify)
{
    err_t ret = ERR_OK;
    static char jsonStr[2048]; // data is not a NULL-terminated string, therefore we need to pre-process it

    if (nullptr == jsonData) return ERR_INVALID_ARG;
    if (jsonDataLen < 2) return ERR_INVALID_ARG; // check for minimum data length ("{}")
    if (jsonDataLen > (sizeof(jsonStr) - 1)) return ERR_INVALID_ARG; // chekc for maximum buffer space

    if (pdFALSE == xSemaphoreTake(configMutex, lockAcquireTimeout)) {
        ESP_LOGE(logTag, "Couldn't acquire config lock within timeout!");
        ret = ERR_TIMEOUT;
    } else {
        ESP_LOGI(logTag, "Parsing hardware config update.");

        static battery_config_t batteryTemp;
        static reservoir_config_t reservoirTemp;

        pwrMgr.setKeepAwakeForce(true);

        memcpy(jsonStr, jsonData, sizeof(char) * jsonDataLen);
        jsonStr[jsonDataLen] = 0;

        cJSON* root = cJSON_ParseWithOpts(jsonStr, nullptr, true);
        if(nullptr != root) {
            cJSON* disableBatteryCheckItem = cJSON_GetObjectItem(root, "disableBatteryCheck");
            cJSON* battCriticalThresholdMilliItem = cJSON_GetObjectItem(root, "battCriticalThresholdMilli");
            cJSON* battLowThresholdMilliItem = cJSON_GetObjectItem(root, "battLowThresholdMilli");
            cJSON* battOkThresholdMilliItem =cJSON_GetObjectItem(root, "battOkThresholdMilli");
            cJSON* disableReservoirCheckItem = cJSON_GetObjectItem(root, "disableReservoirCheck");
            cJSON* fillLevelMaxValItem = cJSON_GetObjectItem(root, "fillLevelMaxVal");
            cJSON* fillLevelMinValItem = cJSON_GetObjectItem(root, "fillLevelMinVal");
            cJSON* fillLevelCriticalThresholdPercent10Item = cJSON_GetObjectItem(root, "fillLevelCriticalThresholdPercent10");
            cJSON* fillLevelLowThresholdPercent10Item = cJSON_GetObjectItem(root, "fillLevelLowThresholdPercent10");
            cJSON* fillLevelHysteresisPercent10Item = cJSON_GetObjectItem(root, "fillLevelHysteresisPercent10");

            if( (nullptr != disableBatteryCheckItem) && cJSON_IsBool(disableBatteryCheckItem) &&
                (nullptr != battCriticalThresholdMilliItem) && cJSON_IsNumber(battCriticalThresholdMilliItem) &&
                (nullptr != battLowThresholdMilliItem) && cJSON_IsNumber(battLowThresholdMilliItem) &&
                (nullptr != battOkThresholdMilliItem) && cJSON_IsNumber(battOkThresholdMilliItem) &&
                (nullptr != disableReservoirCheckItem) && cJSON_IsBool(disableReservoirCheckItem) &&
                (nullptr != fillLevelMaxValItem) && cJSON_IsNumber(fillLevelMaxValItem) &&
                (nullptr != fillLevelMinValItem) && cJSON_IsNumber(fillLevelMinValItem) &&
                (nullptr != fillLevelCriticalThresholdPercent10Item) && cJSON_IsNumber(fillLevelCriticalThresholdPercent10Item) &&
                (nullptr != fillLevelLowThresholdPercent10Item) && cJSON_IsNumber(fillLevelLowThresholdPercent10Item) &&
                (nullptr != fillLevelHysteresisPercent10Item) && cJSON_IsNumber(fillLevelHysteresisPercent10Item) )
            {
                batteryTemp.disableBatteryCheck = cJSON_IsTrue(disableBatteryCheckItem);
                batteryTemp.battCriticalThresholdMilli = battCriticalThresholdMilliItem->valueint;
                batteryTemp.battLowThresholdMilli = battLowThresholdMilliItem->valueint;
                batteryTemp.battOkThresholdMilli = battOkThresholdMilliItem->valueint;

                reservoirTemp.disableReservoirCheck = cJSON_IsTrue(disableReservoirCheckItem);
                reservoirTemp.fillLevelMaxVal = fillLevelMaxValItem->valueint;
                reservoirTemp.fillLevelMinVal = fillLevelMinValItem->valueint;
                reservoirTemp.fillLevelCriticalThresholdPercent10 = fillLevelCriticalThresholdPercent10Item->valueint;
                reservoirTemp.fillLevelLowThresholdPercent10 = fillLevelLowThresholdPercent10Item->valueint;
                reservoirTemp.fillLevelHysteresisPercent10 = fillLevelHysteresisPercent10Item->valueint;
            } else {
                ESP_LOGE(logTag, "Some mandatory hardware settings not found.");
                ret = ERR_SETTINGS_INVALID;
            }
        } else {
            ESP_LOGE(logTag, "Parsing JSON tree failed!");
            ret = ERR_INVALID_JSON;
        }

        if(ret == ERR_OK) {
            ESP_LOGI(logTag, "Hardware config successfully parsed.");
            copyBatteryConfigInt(&shadowDataBatteryConfig, batteryTemp);
            copyReservoirConfigInt(&shadowDataReservoirConfig, reservoirTemp);
        }

        cJSON* storePersistentPtr = cJSON_GetObjectItem(root, "storePersistent");
        if( (nullptr != storePersistentPtr) && cJSON_IsBool(storePersistentPtr) && cJSON_IsTrue(storePersistentPtr) ) {
            ESP_LOGI(logTag, "Persistent storage of hardware config requested.");

            cJSON_DetachItemViaPointer(root, storePersistentPtr);

            char* jsonStrModified = cJSON_Print(root);
            int jsonStrModifiedLen = strlen(jsonStrModified);

            if (ERR_OK != writeConfigFile(filenameHardwareConfig, jsonStrModified, jsonStrModifiedLen)) {
                ret = ERR_FILE_IO;
            }
        }

        xSemaphoreGive(configMutex);

        if((ret == ERR_OK) && !noNotify) {
            callHardwareConfigUpdatedHooks();
        }

        pwrMgr.setKeepAwakeForce(false);
    }

    return ret;
}

SettingsManager::err_t SettingsManager::readIrrigationConfigFile()
{
    return readConfigFile(CONFIG_FILE_IRRIGATION);
}

SettingsManager::err_t SettingsManager::readHardwareConfigFile()
{
    return readConfigFile(CONFIG_FILE_HARDWARE);
}

SettingsManager::err_t SettingsManager::readConfigFile(config_file_type_t type)
{
    err_t ret = ERR_OK;
    const char* filename;
    static char settingsBuffer[8192];

    switch(type) {
        case CONFIG_FILE_IRRIGATION:
            filename = filenameIrrigationConfig;
            break;
        case CONFIG_FILE_HARDWARE:
            filename = filenameHardwareConfig;
            break;
        default:
            ret = ERR_INVALID_ARG;
            break;
    }

    if (ret != ERR_OK) {
        ESP_LOGE(logTag, "Invalid config file type specified.");
    } else {
        struct stat st;
        if (stat(filename, &st) == 0) {
            if (pdFALSE == xSemaphoreTake(fileIoMutex, lockAcquireTimeout)) {
                ESP_LOGE(logTag, "Couldn't acquire config lock within timeout!");
                ret = ERR_TIMEOUT;
            } else {
                FILE* f = fopen(filename, "r");
                if (f == NULL) {
                    ESP_LOGW(logTag, "Failed to open irrigation config file for reading.");
                    ret = ERR_FILE_IO;
                } else {
                    size_t bytesRead;

                    bytesRead = fread(settingsBuffer, sizeof(char), sizeof(settingsBuffer), f);
                    fclose(f);

                    if(bytesRead == sizeof(settingsBuffer)) {
                        ESP_LOGW(logTag, "Config file too big for read buffer. Not reading it in.");
                        ret = ERR_FILE_IO;
                    } else if(bytesRead > 0) {
                        switch(type) {
                            case CONFIG_FILE_IRRIGATION:
                                ESP_LOGI(logTag, "Updating irrigation config from file.");
                                ret = updateIrrigationConfig(settingsBuffer, bytesRead, true);
                                break;
                            case CONFIG_FILE_HARDWARE:
                                ESP_LOGI(logTag, "Updating hardware config from file.");
                                ret = updateHardwareConfig(settingsBuffer, bytesRead, true);
                                break;
                            default:
                                ret = ERR_INVALID_ARG;
                                break;
                        }
                    }
                }
                xSemaphoreGive(fileIoMutex);
            }
        } else {
            ESP_LOGW(logTag, "Config file doesn't exist.");
            ret = ERR_FILE_IO;
        }
    }

    return ret;
}

SettingsManager::err_t SettingsManager::writeConfigFile(const char* const filename, const char* const jsonData, int jsonDataLen)
{
    err_t ret = ERR_OK;

    if (pdFALSE == xSemaphoreTake(fileIoMutex, lockAcquireTimeout)) {
        ESP_LOGE(logTag, "Couldn't acquire config lock within timeout!");
        ret = ERR_TIMEOUT;
    } else {
        FILE* f = fopen(filename, "w");
        if (f == NULL) {
            ESP_LOGW(logTag, "Failed to open config file for writing.");
            ret = ERR_FILE_IO;
        } else {
            size_t bytesWritten;

            bytesWritten = fwrite(jsonData, sizeof(char), jsonDataLen, f);
            fclose(f);

            if(bytesWritten != jsonDataLen) {
                ESP_LOGW(logTag, "Error writing config file. Deleting it.");
                unlink(filename);
                ret = ERR_FILE_IO;
            } else {
                ESP_LOGI(logTag, "Config file written successfully.");
            }

        }
        xSemaphoreGive(fileIoMutex);
    }

    return ret;
}

SettingsManager::err_t SettingsManager::copyZonesAndEvents(
    irrigation_zone_cfg_t* zones, IrrigationEvent* events, bool* eventsUsed)
{
    err_t ret = ERR_OK;

    if((nullptr == zones) || (nullptr == events) || (nullptr == eventsUsed)) {
        return ERR_INVALID_ARG;
    }

    if(pdFALSE == xSemaphoreTake(configMutex, lockAcquireTimeout)) {
        ESP_LOGE(logTag, "Couldn't acquire config lock within timeout!");
        ret = ERR_TIMEOUT;
    } else {
        memcpy(zones, shadowDataIrrigationConfig.zones, sizeof(irrigation_zone_cfg_t) * irrigationZoneCfgElements);
        // TBD: handling of single shot event; currently not implemented in IrrigationPlanner
        for(int i = 0; i < irrigationPlannerNumNormalEvents; i++) {
            events[i] = shadowDataIrrigationConfig.events[i];
            eventsUsed[i] = shadowDataIrrigationConfig.eventsUsed[i];
        }

        xSemaphoreGive(configMutex);
    }

    return ret;
}

void SettingsManager::copyBatteryConfigInt(battery_config_t* dst, battery_config_t& src)
{
    dst->disableBatteryCheck = src.disableBatteryCheck;
    dst->battCriticalThresholdMilli = src.battCriticalThresholdMilli;
    dst->battLowThresholdMilli = src.battLowThresholdMilli;
    dst->battOkThresholdMilli = src.battOkThresholdMilli;
}

void SettingsManager::copyReservoirConfigInt(reservoir_config_t* dst, reservoir_config_t& src)
{
    dst->disableReservoirCheck = src.disableReservoirCheck;
    dst->fillLevelMaxVal = src.fillLevelMaxVal;
    dst->fillLevelMinVal = src.fillLevelMinVal;
    dst->fillLevelCriticalThresholdPercent10 = src.fillLevelCriticalThresholdPercent10;
    dst->fillLevelLowThresholdPercent10 = src.fillLevelLowThresholdPercent10;
    dst->fillLevelHysteresisPercent10 = src.fillLevelHysteresisPercent10;
}

SettingsManager::err_t SettingsManager::copyBatteryConfig(battery_config_t* dst)
{
    err_t ret = ERR_OK;

    if(nullptr == dst) return ERR_INVALID_ARG;

    if(pdFALSE == xSemaphoreTake(configMutex, lockAcquireTimeout)) {
        ESP_LOGE(logTag, "Couldn't acquire config lock within timeout!");
        ret = ERR_TIMEOUT;
    } else {
        copyBatteryConfigInt(dst, shadowDataBatteryConfig);
        xSemaphoreGive(configMutex);
    }

    return ret;
}

SettingsManager::err_t SettingsManager::copyReservoirConfig(reservoir_config_t* dst)
{
    err_t ret = ERR_OK;

    if(nullptr == dst) return ERR_INVALID_ARG;

    if(pdFALSE == xSemaphoreTake(configMutex, lockAcquireTimeout)) {
        ESP_LOGE(logTag, "Couldn't acquire config lock within timeout!");
        ret = ERR_TIMEOUT;
    } else {
        copyReservoirConfigInt(dst, shadowDataReservoirConfig);
        xSemaphoreGive(configMutex);
    }

    return ret;
}

SettingsManager::err_t SettingsManager::registerIrrigConfigUpdatedHook(ConfigUpdatedHookFncPtr hook, void* param)
{
    err_t ret = ERR_OK;

    if(pdFALSE == xSemaphoreTake(hookMutex, lockAcquireTimeout)) {
        ESP_LOGE(logTag, "Couldn't acquire hook lock within timeout!");
        ret = ERR_TIMEOUT;
    } else {
        bool slotFound = false;
        for (int i=0; i<numHookTableEntries; i++) {
            if (nullptr == irrigConfigUpdatedHooks[i]) {
                slotFound = true;
                irrigConfigUpdatedHooks[i] = hook;
                irrigConfigUpdatedHookParamPtrs[i] = param;
                break;
            }
        }
        if (!slotFound) {
            ESP_LOGE(logTag, "No free irrigConfigUpdatedHooks slot found.");
            ret = ERR_NO_RESOURCES;
        }

        xSemaphoreGive(hookMutex);
    }

    return ret;
}

SettingsManager::err_t SettingsManager::registerHardwareConfigUpdatedHook(ConfigUpdatedHookFncPtr hook, void* param)
{
    err_t ret = ERR_OK;

    if(pdFALSE == xSemaphoreTake(hookMutex, lockAcquireTimeout)) {
        ESP_LOGE(logTag, "Couldn't acquire hook lock within timeout!");
        ret = ERR_TIMEOUT;
    } else {
        bool slotFound = false;
        for (int i=0; i<numHookTableEntries; i++) {
            if (nullptr == hardwareConfigUpdatedHooks[i]) {
                slotFound = true;
                hardwareConfigUpdatedHooks[i] = hook;
                hardwareConfigUpdatedHookParamPtrs[i] = param;
                break;
            }
        }
        if (!slotFound) {
            ESP_LOGE(logTag, "No free hardwareConfigUpdatedHooks slot found.");
            ret = ERR_NO_RESOURCES;
        }

        xSemaphoreGive(hookMutex);
    }

    return ret;
}

void SettingsManager::callIrrigConfigUpdatedHooks()
{
    if(pdFALSE == xSemaphoreTake(hookMutex, lockAcquireTimeout)) {
        ESP_LOGE(logTag, "Couldn't acquire hook lock within timeout!");
    } else {
        for (int i=0; i<numHookTableEntries; i++) {
            if (nullptr != irrigConfigUpdatedHooks[i]) {
                irrigConfigUpdatedHooks[i](irrigConfigUpdatedHookParamPtrs[i]);
            }
        }
        xSemaphoreGive(hookMutex);
    }
}

void SettingsManager::callHardwareConfigUpdatedHooks()
{
    if(pdFALSE == xSemaphoreTake(hookMutex, lockAcquireTimeout)) {
        ESP_LOGE(logTag, "Couldn't acquire hook lock within timeout!");
    } else {
        for (int i=0; i<numHookTableEntries; i++) {
            if (nullptr != hardwareConfigUpdatedHooks[i]) {
                hardwareConfigUpdatedHooks[i](hardwareConfigUpdatedHookParamPtrs[i]);
            }
        }
        xSemaphoreGive(hookMutex);
    }
}
