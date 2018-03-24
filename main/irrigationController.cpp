#include "irrigationController.h"

// TBD: encapsulate
RTC_DATA_ATTR static time_t lastIrrigEvent = 0;

/**
 * @brief Default constructor, which performs basic initialization,
 * but doesn't start processing.
 */
IrrigationController::IrrigationController(void)
{
    size_t len = strlen(mqttTopicPre) + strlen(mqttStateTopicPost) + 12 + 1;
    mqttStateTopic = (char*) calloc(len, sizeof(char));

    // Format string length + 5 digits voltage in mV + 1 digit batt state + 8 digits for battery state string +
    // 4 digits (fillLevel * 10) + 1 digit for reservoir state,
    // 8 digits for reservoir state string,
    // 2 digits per active output + ', ' as seperator
    // 6 digits per active output string + ', ' as seperator
    // 19 digits for the next event datetime
    // 19 digits for the last SNTP sync datetime
    // 19 digits for the next SNTP sync datetime.
    // Format string is a bit too long, but don't care too mich about those few bytes
    mqttStateDataMaxLen = strlen(mqttStateDataFmt) + 5 + 1 + 8 + 4 + 1 + 8 + 
        (4+8)*(OutputController::intChannels+OutputController::extChannels) + 
        19 + 19 + 19 + 1;
    mqttStateData = (char*) calloc(mqttStateDataMaxLen, sizeof(char));

    // Reserve space for active outputs
    state.activeOutputs.clear();
    state.activeOutputs.reserve(OutputController::intChannels+OutputController::extChannels);

    // Prepare time system event hook to react properly on time changes
    // Note: hook registration will be performed by the main thread, because the
    // IrrigationPlanner instance will be created before the TimeSystem is initialized.
    timeEvents = xEventGroupCreate();
}

/**
 * @brief Default destructor, which cleans up allocated data.
 */
IrrigationController::~IrrigationController(void)
{
    // TBD: graceful shutdown of task
    if(mqttStateTopic) free(mqttStateTopic);
    if(mqttStateData) free(mqttStateData);
}

/**
 * @brief Start the IrrigationController processing task.
 */
void IrrigationController::start(void)
{
    taskHandle = xTaskCreateStatic(taskFunc, "irrig_ctrl_task", taskStackSize, (void*) this, taskPrio, taskStack, &taskBuf);
    if(NULL != taskHandle) {
        ESP_LOGI(logTag, "IrrigationController task created. Starting.");
    } else {
        ESP_LOGE(logTag, "IrrigationController task creation failed!");
    }
}

/**
 * @brief This is the IrrigationController processing task.
 * 
 * It implements the control logic of the class. For details see the class description.
 * 
 * @param params Task parameters. Used to pass in the actual IrrigationController
 * instance the task function is running for.
 */
void IrrigationController::taskFunc(void* params)
{
    IrrigationController* caller = (IrrigationController*) params;

    EventBits_t events;
    TickType_t wait, loopStartTicks, nowTicks;
    time_t now, nextIrrigEvent, sntpNextSync;
    bool irrigOk;

    // Wait for WiFi to come up. TBD: make configurable (globally), implement WiFiManager for that
    wait = portMAX_DELAY;
    if(caller->wifiConnectedWaitMillis >= 0) {
        wait = pdMS_TO_TICKS(caller->wifiConnectedWaitMillis);
    }
    events = xEventGroupWaitBits(wifiEvents, wifiEventConnected, pdFALSE, pdTRUE, wait);
    if(0 != (events & wifiEventConnected)) {
        ESP_LOGD(caller->logTag, "WiFi connected.");
    } else {
        ESP_LOGE(caller->logTag, "WiFi didn't come up within timeout!");
    }

    // Check if we have a valid time
    if(!TimeSystem_TimeIsSet()) {
        // Time hasen't been, so assume something to get operating somehow.
        // After setting it here, it will be stored in the RTC, so next time we come up it
        // will not be set to the default again.
        TimeSystem_SetTime(01, 01, 2018, 06, 00, 00);
        ESP_LOGW(caller->logTag, "Time hasn't been set yet. Setting default time: 2018-01-01, 06:00:00.");
    } else {
        ESP_LOGD(caller->logTag, "Time is already set.");
    }

    // Properly initialize some time vars
    {
        now = time(nullptr);
        if(lastIrrigEvent == 0) {
            struct tm nowTm;
            localtime_r(&now, &nowTm);
            nowTm.tm_sec--;
            lastIrrigEvent = mktime(&nowTm);
        }
    }

    // Register time system hook
    TimeSystem_RegisterHook(timeSytemEventsHookDispatch, caller);

    while(1) {
        loopStartTicks = xTaskGetTickCount();

        // *********************
        // Power up needed peripherals, DCDC, ...
        // *********************
        // Peripheral enable will power up the DCDC as well as the RS232 driver
        if(!pwrMgr.getPeripheralEnable()) {
            ESP_LOGD(caller->logTag, "Bringing up DCDC + RS232 driver.");
            pwrMgr.setPeripheralEnable(true);
            // Wait for stable onboard peripherals
            vTaskDelay(pdMS_TO_TICKS(peripheralEnStartupMillis));
        }

        // Enable external sensor power
        if((!caller->disableReservoirCheck) && (!pwrMgr.getPeripheralExtSupply())) {
            ESP_LOGD(caller->logTag, "Powering external sensors.");
            pwrMgr.setPeripheralExtSupply(true);
            // Wait for external sensors to power up properly
            vTaskDelay(pdMS_TO_TICKS(peripheralExtSupplyMillis));
        }

        // *********************
        // Fetch sensor data
        // *********************
        // Battery voltage
        caller->state.battVoltage = pwrMgr.getSupplyVoltageMilli();
        if(caller->disableBatteryCheck) {
            caller->state.battState = PowerManager::BATT_DISABLED;
        } else {
            caller->state.battState = pwrMgr.getBatteryState(caller->state.battVoltage);
        }
        ESP_LOGD(caller->logTag, "Battery voltage: %02.2f V (%s)", roundf(caller->state.battVoltage * 0.1f) * 0.01f,
            BATT_STATE_TO_STR(caller->state.battState));

        // Get fill level of the reservoir, if not disabled.
        if(!caller->disableReservoirCheck) {
            caller->state.fillLevel = fillSensor.getFillLevel();
            if(caller->state.fillLevel >= fillLevelLowThreshold) {
                caller->state.reservoirState = RESERVOIR_OK;
            } else if (caller->state.fillLevel >= fillLevelCriticalThreshold) {
                caller->state.reservoirState = RESERVOIR_LOW;
            } else {
                caller->state.reservoirState = RESERVOIR_CRITICAL;
            }
        } else {
            caller->state.fillLevel = -2;
            caller->state.reservoirState = RESERVOIR_DISABLED;
        }
        ESP_LOGD(caller->logTag, "Reservoir fill level: %d (%s)", caller->state.fillLevel, 
            RESERVOIR_STATE_TO_STR(caller->state.reservoirState));

        // Power down external supply already. Not needed anymore.
        if(pwrMgr.getPeripheralExtSupply()) {
            pwrMgr.setPeripheralExtSupply(false);
            ESP_LOGD(caller->logTag, "Sensors powered down.");
        }

        // TBD: Get weather forecast
        // TBD: Get local weather data

        // Check system preconditions for the irrigation, i.e. battery state and reservoir fill level
        irrigOk = true;
        if(caller->state.battState == PowerManager::BATT_CRITICAL) {
            irrigOk = false;
        }
        if(caller->state.reservoirState == RESERVOIR_CRITICAL) {
            irrigOk = false;
        }

        // Check if system conditions got critical and outputs are active
        if(outputCtrl.anyOutputsActive() && !irrigOk) {
            ESP_LOGW(caller->logTag, "Active outputs detected, but system conditions critical! Disabling them for safety.");
            outputCtrl.disableAllOutputs();
        }

        // *********************
        // Irrigation
        // *********************
        int millisTillNextEvent;
        bool eventsToProcess = true;
        while(eventsToProcess) {
            now = time(nullptr);

            // Check if the system time has been (re)set
            events = xEventGroupClearBits(caller->timeEvents, caller->timeEventTimeSet | caller->timeEventTimeSetSntp);
            if(0 != (events & caller->timeEventTimeSet)) {
                ESP_LOGI(caller->logTag, "Time set detected. Resetting event processing.");
                // Recalculate lastIrrigEvent to not process events that haven't really 
                // happend in-between last time and now.
                struct tm nowTm;
                localtime_r(&now, &nowTm);
                nowTm.tm_sec--;
                lastIrrigEvent = mktime(&nowTm);

                // Calculate the next SNTP sync only if this was a manual time set event, otherwise
                // the next sync time has already been set above
                if(0 == (events & caller->timeEventTimeSetSntp)) {
                    struct tm sntpNextSyncTm;
                    localtime_r(&now, &sntpNextSyncTm);
                    sntpNextSyncTm.tm_hour += caller->sntpResyncIntervalHours;
                    sntpNextSync = mktime(&sntpNextSyncTm);
                    TimeSystem_SetNextSntpSync(sntpNextSync);
                }

                // Disable all outputs to abort any irrigations that may have been started.
                outputCtrl.disableAllOutputs();
            }

            nextIrrigEvent = irrigPlanner.getNextEventTime(lastIrrigEvent, true);
            caller->state.nextIrrigEvent = nextIrrigEvent;
            millisTillNextEvent = (int) round(difftime(nextIrrigEvent, now) * 1000.0);

            // // Publish state with the updated next event time
            // caller->publishStateUpdate();

            // Perform event actions if it is time now
            now = time(nullptr);
            double diffTime = difftime(nextIrrigEvent, now);
            // Event within delta or have we even overshot the target?
            if((nextIrrigEvent != 0) && ((fabs(diffTime) < 1.0) || (diffTime <= -1.0))) {
                TimeSystem_LogTime();

                if(!irrigOk) {
                    ESP_LOGE(caller->logTag, "Critical system conditions detected! Dropping irrigation.");
                }

                struct tm eventTm;
                localtime_r(&nextIrrigEvent, &eventTm);
                ESP_LOGI(caller->logTag, "Actions to perform for event at %02d.%02d.%04d %02d:%02d:%02d",
                    eventTm.tm_mday, eventTm.tm_mon+1, 1900+eventTm.tm_year,
                    eventTm.tm_hour, eventTm.tm_min, eventTm.tm_sec);

                std::vector<IrrigationEvent::ch_cfg_t> chCfg;
                irrigPlanner.getEventChannelConfig(nextIrrigEvent, &chCfg);
                for(std::vector<IrrigationEvent::ch_cfg_t>::iterator chIt = chCfg.begin(); chIt != chCfg.end(); chIt++) {
                    ESP_LOGI(caller->logTag, "* Channel: %s, state: %s", 
                        CH_MAP_TO_STR((*chIt).chNum), (*chIt).switchOn ? "ON" : "OFF");

                    // Only enable outputs when preconditions are met; disabling is always okay.
                    if(irrigOk || !(*chIt).switchOn) {
                        outputCtrl.setOutput((OutputController::ch_map_t) (*chIt).chNum, (*chIt).switchOn);
                        caller->updateStateActiveOutputs((*chIt).chNum, (*chIt).switchOn);
                    }
                }

                lastIrrigEvent = nextIrrigEvent;
            } else {
                eventsToProcess = false;
            }

            // Publish state with the updated next event time + active outputs
            caller->state.sntpLastSync = TimeSystem_GetLastSntpSync();
            caller->state.sntpNextSync = TimeSystem_GetNextSntpSync();
            caller->publishStateUpdate();
        }

        // *********************
        // SNTP resync
        // *********************
        bool skipSntpResync = false;

        // Skip resync in case an upcoming event is close
        millisTillNextEvent = (int) round(difftime(nextIrrigEvent, time(nullptr)) * 1000.0);
        if(millisTillNextEvent <= caller->noSntpResyncRangeMillis) {
            skipSntpResync = true;
            ESP_LOGD(caller->logTag, "Skipping SNTP (re)sync, because next upcoming event is too close.");
        }

        // Skip rsync if we are offline
        events = xEventGroupWaitBits(wifiEvents, wifiEventConnected, pdFALSE, pdTRUE, 0);
        if (0 == (events & wifiEventConnected)) {
            skipSntpResync = true;
            ESP_LOGD(caller->logTag, "Skipping SNTP (re)sync, because we are offline.");
        }

        // Skip resync if outputs are active, because the new time being set would disable
        // all outputs and they would be turned back on again
        if(outputCtrl.anyOutputsActive()) {
            skipSntpResync = true;
            ESP_LOGD(caller->logTag, "Skipping SNTP (re)sync, because outputs are active.");
        }

        if(!skipSntpResync) {
            // Request an SNTP resync if it hasn't happend at all or the next scheduled sync is due,
            sntpNextSync = TimeSystem_GetNextSntpSync();
            if((sntpNextSync == 0) || (difftime(sntpNextSync, time(nullptr)) <= 0.0f)) {
                ESP_LOGI(caller->logTag, "Requesting an SNTP time (re)sync.");
                TimeSystem_SntpRequest();

                // Wait for the sync
                events = xEventGroupWaitBits(caller->timeEvents, caller->timeEventTimeSetSntp, pdTRUE, pdTRUE, pdMS_TO_TICKS(caller->timeResyncWaitMillis));
                if(0 != (events & caller->timeEventTimeSetSntp)) {
                    ESP_LOGI(caller->logTag, "SNTP time (re)sync was successful.");
                } else {
                    ESP_LOGW(caller->logTag, "SNTP time (re)sync wasn't successful within timeout.");
                }

                // Stop the SNTP background process, so it won't intefere with running irrigations
                TimeSystem_SntpStop();

                // Calculate the next SNTP sync
                struct tm sntpNextSyncTm;
                sntpNextSync = time(nullptr);
                localtime_r(&sntpNextSync, &sntpNextSyncTm);
                sntpNextSyncTm.tm_hour += caller->sntpResyncIntervalHours;
                sntpNextSync = mktime(&sntpNextSyncTm);
                TimeSystem_SetNextSntpSync(sntpNextSync);

                // Update SNTP info
                caller->state.sntpLastSync = TimeSystem_GetLastSntpSync();
                caller->state.sntpNextSync = TimeSystem_GetNextSntpSync();
            }
        }

        // *********************
        // Sleep preparation
        // *********************
        // Get current time again for sleep calculation
        now = time(nullptr);

        // Check if the system time has been (re)set
        events = xEventGroupClearBits(caller->timeEvents, caller->timeEventTimeSet | caller->timeEventTimeSetSntp);
        if(0 != (events & caller->timeEventTimeSet)) {
            // Recalculate lastIrrigEvent and nextIrrig to not process events that haven't really 
            // happend in-between last time and now.
            ESP_LOGI(caller->logTag, "Time set detected. Resetting event processing.");
            struct tm nowTm;
            localtime_r(&now, &nowTm);
            nowTm.tm_sec--;
            lastIrrigEvent = mktime(&nowTm);
            nextIrrigEvent = irrigPlanner.getNextEventTime(lastIrrigEvent, true);

            // Calculate the next SNTP sync only if this was a manual time set event, otherwise
            // the next sync time has already been set above
            if(0 == (events & caller->timeEventTimeSetSntp)) {
                struct tm sntpNextSyncTm;
                localtime_r(&now, &sntpNextSyncTm);
                sntpNextSyncTm.tm_hour += caller->sntpResyncIntervalHours;
                sntpNextSync = mktime(&sntpNextSyncTm);
                TimeSystem_SetNextSntpSync(sntpNextSync);
            }

            // Disable all outputs to abort any irrigations that may have been started.
            outputCtrl.disableAllOutputs();

            // Update next irrigation event and publish (also the new SNTP info set above)
            caller->state.nextIrrigEvent = nextIrrigEvent;
            caller->publishStateUpdate();
        }

        millisTillNextEvent = (int) round(difftime(nextIrrigEvent, now) * 1000.0);

        // Power down the DCDC if no outputs are active.
        if(!outputCtrl.anyOutputsActive()) {
            pwrMgr.setPeripheralEnable(false);
            ESP_LOGD(caller->logTag, "DCDC + RS232 driver powered down.");
        }

        if(pwrMgr.getKeepAwake()) {
            // Calculate loop runtime and compensate the sleep time with it
            nowTicks = xTaskGetTickCount();
            int loopRunTimeMillis = portTICK_RATE_MS * ((nowTicks > loopStartTicks) ? 
                (nowTicks - loopStartTicks) :
                (portMAX_DELAY - loopStartTicks + nowTicks + 1));
            ESP_LOGD(caller->logTag, "Loop runtime %d ms.", loopRunTimeMillis);

            int sleepMillis = caller->wakeupIntervalKeepAwakeMillis - loopRunTimeMillis;
            if(sleepMillis > millisTillNextEvent) sleepMillis = millisTillNextEvent - caller->preEventMillis;
            if(sleepMillis < 0) sleepMillis = 0;

            ESP_LOGD(caller->logTag, "Task is going to sleep for %d ms.", sleepMillis);
            vTaskDelay(pdMS_TO_TICKS(sleepMillis));
        } else {
            // Wait to get all updates through
            if(!mqttMgr.waitAllPublished(caller->mqttAllPublishedWaitMillis)) {
                ESP_LOGW(caller->logTag, "Waiting for MQTT to publish all messages didn't complete within timeout.");
            }

            // TBD: stop webserver, mqtt and other stuff

            // Calculate loop runtime and compensate the sleep time with it
            nowTicks = xTaskGetTickCount();
            int loopRunTimeMillis = portTICK_RATE_MS * ((nowTicks > loopStartTicks) ? 
                (nowTicks - loopStartTicks) :
                (portMAX_DELAY - loopStartTicks + nowTicks + 1));
            ESP_LOGD(caller->logTag, "Loop runtime %d ms.", loopRunTimeMillis);

            int sleepMillis = caller->wakeupIntervalMillis - loopRunTimeMillis;
            if(sleepMillis > millisTillNextEvent) sleepMillis = millisTillNextEvent - caller->preEventMillisDeepSleep;
            if(sleepMillis < 0) sleepMillis = 0;

            // Check if there is enough wakeup time
            if( (sleepMillis < caller->noDeepSleepRangeMillis) && 
                (millisTillNextEvent <= caller->noDeepSleepRangeMillis) ) {
                ESP_LOGD(caller->logTag, "Event coming up sooner than deep sleep wakeup time. "
                    "Task is going to sleep for %d ms insted of deep sleep.", sleepMillis);
                vTaskDelay(pdMS_TO_TICKS(sleepMillis));
            }
            // Check if any outputs are active, deep sleep would kill them!
            else if(outputCtrl.anyOutputsActive()) {
                ESP_LOGD(caller->logTag, "Outputs active. Task is going to sleep for %d ms insted of deep sleep.", sleepMillis);
                vTaskDelay(pdMS_TO_TICKS(sleepMillis));
            }
            else {
                ESP_LOGD(caller->logTag, "Preparing deep sleep for %d ms.", sleepMillis);
                pwrMgr.gotoSleep(sleepMillis);
            }
        }
    }

    vTaskDelete(NULL);

    ESP_LOGE(caller->logTag, "Task unexpectetly exited! Performing reboot.");
    //pwrMgr->reboot();
}

/**
 * @brief Update active outputs list in internal state structure.
 * 
 * @param chNum Output channel to update
 * @param active Wether or not the channel is now active.
 */
void IrrigationController::updateStateActiveOutputs(uint32_t chNum, bool active)
{
    bool inserted = false;
    for(std::vector<uint32_t>::iterator it = state.activeOutputs.begin(); it != state.activeOutputs.end(); it++) {
        if(!active) {
            if(*it == chNum) {
                state.activeOutputs.erase(it);
                break;
            }
        } else {
            if(*it > chNum) {
                it = state.activeOutputs.insert(it, chNum);
                inserted = true;
                break;
            } else if(*it == chNum) {
                inserted = true;
                break;
            }
        }
    }

    if(active && !inserted) {
        state.activeOutputs.push_back(chNum);
    }
}

/**
 * @brief Publish currently stored state via MQTT.
 */
void IrrigationController::publishStateUpdate(void)
{
    static uint8_t mac_addr[6];
    static char timeStr[20];
    static char sntpLastSyncTimeStr[20];
    static char sntpNextSyncTimeStr[20];
    static char activeOutputs[4*(OutputController::intChannels+OutputController::extChannels)+1];
    static char activeOutputsStr[8*(OutputController::intChannels+OutputController::extChannels)+1];
    size_t preLen;
    size_t postLen;

    if(false == mqttMgr.waitConnected(mqttConnectedWaitMillis)) {
        ESP_LOGW(logTag, "MQTT manager has no connection after timeout.")
    } else {
        if(!mqttPrepared) {
            if(ESP_OK == esp_wifi_get_mac(ESP_IF_WIFI_STA, mac_addr)) {
                preLen = strlen(mqttTopicPre);
                postLen = strlen(mqttStateTopicPost);
                memcpy(mqttStateTopic, mqttTopicPre, preLen);
                for(int i=0; i<6; i++) {
                    sprintf(&mqttStateTopic[preLen+i*2], "%02x", mac_addr[i]);
                }
                memcpy(&mqttStateTopic[preLen+12], mqttStateTopicPost, postLen);
                mqttStateTopic[preLen+12+postLen] = 0;
                mqttPrepared = true;
            } else {
                ESP_LOGE(logTag, "Getting MAC address failed!");
            }
        }

        if(mqttPrepared) {
            // Prepare next irrigation string
            struct tm nextIrrigEventTm;
            localtime_r(&state.nextIrrigEvent, &nextIrrigEventTm);
            strftime(timeStr, 20, "%Y-%m-%d %H:%M:%S", &nextIrrigEventTm);

            // Prepare next+last SNTP sync strings
            struct tm sntpLastSyncTm;
            localtime_r(&state.sntpLastSync, &sntpLastSyncTm);
            strftime(sntpLastSyncTimeStr, 20, "%Y-%m-%d %H:%M:%S", &sntpLastSyncTm);

            struct tm sntpNextSyncTm;
            localtime_r(&state.sntpNextSync, &sntpNextSyncTm);
            strftime(sntpNextSyncTimeStr, 20, "%Y-%m-%d %H:%M:%S", &sntpNextSyncTm);

            // Prepare active outputs strings
            activeOutputs[0] = '\0';
            activeOutputsStr[0] = '\0';
            for(std::vector<uint32_t>::iterator it = state.activeOutputs.begin(); it != state.activeOutputs.end(); it++) {
                snprintf(activeOutputs, sizeof(activeOutputs) / sizeof(activeOutputs[0]),
                    "%s%s%d", activeOutputs, (it==state.activeOutputs.begin()) ? "" : ", ", *it);
                snprintf(activeOutputsStr, sizeof(activeOutputsStr) / sizeof(activeOutputsStr[0]),
                    "%s%s\"%s\"", activeOutputsStr, (it==state.activeOutputs.begin()) ? "" : ", ", CH_MAP_TO_STR(*it));
            }

            // Build the string to be send
            size_t actualLen = snprintf(mqttStateData, mqttStateDataMaxLen, mqttStateDataFmt,
                state.battVoltage, state.battState, BATT_STATE_TO_STR(state.battState), 
                state.fillLevel, state.reservoirState, RESERVOIR_STATE_TO_STR(state.reservoirState),
                activeOutputs, activeOutputsStr, timeStr, sntpLastSyncTimeStr, sntpNextSyncTimeStr);
            mqttMgr.publish(mqttStateTopic, mqttStateData, actualLen, MqttManager::QOS_EXACTLY_ONCE, false);
        }
    }
}

/**
 * @brief This function handles events when a new time has been set.
 * 
 * This is important for the main processing thread to properly react to discontinuous 
 * time changes.
 * 
 */
void IrrigationController::timeSytemEventHandler(time_system_event_t events)
{
    // set an event group bit for the processing task
    if(0 != (events & TimeSystem_timeEventTimeSet)) {
        xEventGroupSetBits(timeEvents, timeEventTimeSet);
    }
    if(0 != (events & TimeSystem_timeEventTimeSetSntp)) {
        xEventGroupSetBits(timeEvents, timeEventTimeSetSntp);
    }
}

/**
 * @brief Static hook dispatcher, which delegates time events to the correct IrrigationController
 * instance.
 * 
 * Currently, only 'time set' events are handled and relevant.
 * 
 * @param param Pointer to the actual IrrigationController instance
 * @param event Concrete event from the TimeSystem.
 */
void IrrigationController::timeSytemEventsHookDispatch(void* param, time_system_event_t events)
{
    IrrigationController* controller = (IrrigationController*) param;

    if(nullptr == controller) {
        ESP_LOGE("unkown", "No valid IrrigationController available to dispatch time system events to!")
    } else {
        if(0 != events) {
            controller->timeSytemEventHandler(events);
        }
    }
}
