#include "settingsManager.h"

#include <stdio.h>

#include "globalComponents.h"

/**
 * @brief Default constructor, which performs basic initialization.
 */
SettingsManager::SettingsManager(void)
{
    // Clear event and zone shadow storages
    clearZoneData(shadowData);
    clearEventData(shadowData);

    configMutex = xSemaphoreCreateMutexStatic(&configMutexBuf);
    fileIoMutex = xSemaphoreCreateMutexStatic(&fileIoMutexBuf);
}

/**
 * @brief Default destructor, which cleans up allocated data.
 */
SettingsManager::~SettingsManager(void)
{
    if(configMutex) vSemaphoreDelete(configMutex);
    if(fileIoMutex) vSemaphoreDelete(fileIoMutex);
}

void SettingsManager::clearZoneData(settings_container_t& settings)
{
    for(int i = 0; i < irrigationPlannerNumZones; i++) {
        settings.zones[i].name[irrigationZoneCfgNameLen] = '\0';
        for(int j = 0; j < irrigationZoneCfgElements; j++) {
            settings.zones[i].chEnabled[j] = false;
        }
    }
}

void SettingsManager::clearEventData(settings_container_t& settings)
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

SettingsManager::err_t SettingsManager::updateIrrigationConfig(const char* const jsonData, int jsonDataLen)
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
        static settings_container_t settingsTemp;

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
            ESP_LOGD(logTag, "Zone and event data successfully parsed.");
            memcpy(shadowData.zones, settingsTemp.zones, sizeof(irrigation_zone_cfg_t) * irrigationZoneCfgElements);
            for(int i = 0; i < irrigationPlannerNumNormalEvents; i++) {
                shadowData.events[i] = settingsTemp.events[i];
                shadowData.eventsUsed[i] = settingsTemp.eventsUsed[i];
            }
        }

        cJSON* storePersistentPtr = cJSON_GetObjectItem(root, "storePersistent");
        if( (nullptr != storePersistentPtr) && cJSON_IsBool(storePersistentPtr) && cJSON_IsTrue(storePersistentPtr) ) {
            ESP_LOGI(logTag, "Persistent storage of irrigation configuration requested.");

            cJSON_DetachItemViaPointer(root, storePersistentPtr);

            char* jsonStrModified = cJSON_Print(root);
            int jsonStrModifiedLen = strlen(jsonStrModified);

            if (ERR_OK != writeIrrigationConfigFile(jsonStrModified, jsonStrModifiedLen)) {
                ret = ERR_FILE_IO;
            }
        }

        xSemaphoreGive(configMutex);

        if(ret == ERR_OK) {
            irrigPlanner.configurationUpdated();
        }

        pwrMgr.setKeepAwakeForce(false);
    }

    return ret;
}

SettingsManager::err_t SettingsManager::readIrrigationConfigFile()
{
    err_t ret = ERR_OK;
    static char settingsBuffer[8192];

    struct stat st;
    if (stat(filenameIrrigationConfig, &st) == 0) {
        if (pdFALSE == xSemaphoreTake(fileIoMutex, lockAcquireTimeout)) {
            ESP_LOGE(logTag, "Couldn't acquire config lock within timeout!");
            ret = ERR_TIMEOUT;
        } else {
            FILE* f = fopen(filenameIrrigationConfig, "r");
            if (f == NULL) {
                ESP_LOGW(logTag, "Failed to open irrigation config file for reading.");
                ret = ERR_FILE_IO;
            } else {
                size_t bytesRead;

                bytesRead = fread(settingsBuffer, sizeof(char), sizeof(settingsBuffer), f);
                fclose(f);

                if(bytesRead == sizeof(settingsBuffer)) {
                    ESP_LOGW(logTag, "Irrigation config file too big for read buffer. Not reading it in.");
                    ret = ERR_FILE_IO;
                } else if(bytesRead > 0) {
                    ESP_LOGI(logTag, "Updating irrigation config from file.");
                    ret = updateIrrigationConfig(settingsBuffer, bytesRead);
                }
            }
            xSemaphoreGive(fileIoMutex);
        }
    } else {
        ESP_LOGW(logTag, "Irrigation config file doesn't exist.");
        ret = ERR_FILE_IO;
    }

    return ret;
}

SettingsManager::err_t SettingsManager::writeIrrigationConfigFile(const char* const jsonData, int jsonDataLen)
{
    err_t ret = ERR_OK;

    if (pdFALSE == xSemaphoreTake(fileIoMutex, lockAcquireTimeout)) {
        ESP_LOGE(logTag, "Couldn't acquire config lock within timeout!");
        ret = ERR_TIMEOUT;
    } else {
        FILE* f = fopen(filenameIrrigationConfig, "w");
        if (f == NULL) {
            ESP_LOGW(logTag, "Failed to open irrigation config file for writing.");
            ret = ERR_FILE_IO;
        } else {
            size_t bytesWritten;

            bytesWritten = fwrite(jsonData, sizeof(char), jsonDataLen, f);
            fclose(f);

            if(bytesWritten != jsonDataLen) {
                ESP_LOGW(logTag, "Error writing irrigation config file. Deleting it.");
                unlink(filenameIrrigationConfig);
                ret = ERR_FILE_IO;
            } else {
                ESP_LOGI(logTag, "Irrigation config file written successfully.");
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
        memcpy(zones, shadowData.zones, sizeof(irrigation_zone_cfg_t) * irrigationZoneCfgElements);
        // TBD: handling of single shot event; currently not implemented in IrrigationPlanner
        for(int i = 0; i < irrigationPlannerNumNormalEvents; i++) {
            events[i] = shadowData.events[i];
            eventsUsed[i] = shadowData.eventsUsed[i];
        }

        xSemaphoreGive(configMutex);
    }

    return ret;
}
