#include "irrigationController.h"

extern "C" {
    void esp_restart_noos() __attribute__ ((noreturn));
}

// TBD: encapsulate
RTC_DATA_ATTR static IrrigationController::peristent_data_t irrigCtrlPersistentData = {
    .lastIrrigEvent = 0,
    .reservoirState = IrrigationController::RESERVOIR_OK
};

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

    // prepare empty last state
    memset(&lastState, 0, sizeof(state_t));
    lastState.activeOutputs.clear();
    lastState.activeOutputs.reserve(OutputController::intChannels+OutputController::extChannels);

    // Prepare time system event hook to react properly on time changes
    // Note: hook registration will be performed by the main thread, because the
    // IrrigationPlanner instance will be created before the TimeSystem is initialized.
    timeEvents = xEventGroupCreate();

    if(NULL == timeEvents) {
        ESP_LOGE(logTag, "timeEvents event group couldn't be created.");
    }
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
    if(NULL == timeEvents) {
        ESP_LOGE(logTag, "Needed resources haven't been allocated. Not starting the task.");
    } else {
        taskHandle = xTaskCreateStatic(taskFuncDispatch, "irrig_ctrl_task", taskStackSize, (void*) this, taskPrio, taskStack, &taskBuf);
        if(NULL != taskHandle) {
            ESP_LOGI(logTag, "IrrigationController task created. Starting.");
        } else {
            ESP_LOGE(logTag, "IrrigationController task creation failed!");
        }
    }
}

/**
 * @brief This is the IrrigationController processing task dispatcher.
 * 
 * @param params Task parameters. Used to pass in the actual IrrigationController
 * instance the task function is running for.
 */
void IrrigationController::taskFuncDispatch(void* params)
{
    IrrigationController* caller = (IrrigationController*) params;

    caller->taskFunc();
}

/**
 * @brief This is the IrrigationController processing task.
 * 
 * It implements the control logic of the class. For details see the class description.
 */
void IrrigationController::taskFunc()
{
    EventBits_t events;
    TickType_t wait, loopStartTicks, nowTicks;
    time_t now, nextIrrigEvent, sntpNextSync;
    IrrigationPlanner::err_t plannerErr;
    bool irrigOk;
    bool firstRun = true;

    emergencyTimerHandle = xTimerCreateStatic("Emergency reboot timer", emergencyTimerTicks,
        pdFALSE, (void*) 0, emergencyTimerCb, &emergencyTimerBuf);

    if((NULL == emergencyTimerHandle) || (pdPASS != xTimerStart(emergencyTimerHandle, 0))) {
        ESP_LOGE(logTag, "Emergency reboot timer couldn't be setup. Doing our best without it ...");
    }

    // Wait for WiFi to come up. TBD: make configurable (globally), implement WiFiManager for that
    wait = portMAX_DELAY;
    if(wifiConnectedWaitMillis >= 0) {
        wait = pdMS_TO_TICKS(wifiConnectedWaitMillis);
    }
    events = xEventGroupWaitBits(wifiEvents, wifiEventConnected, pdFALSE, pdTRUE, wait);
    if(0 != (events & wifiEventConnected)) {
        ESP_LOGD(logTag, "WiFi connected.");
    } else {
        ESP_LOGE(logTag, "WiFi didn't come up within timeout!");
    }

    // Check if we have a valid time
    if(!TimeSystem_TimeIsSet()) {
        // Time hasen't been, so assume something to get operating somehow.
        // After setting it here, it will be stored in the RTC, so next time we come up it
        // will not be set to the default again.
        TimeSystem_SetTime(01, 01, 2018, 06, 00, 00);
        ESP_LOGW(logTag, "Time hasn't been set yet. Setting default time: 2018-01-01, 06:00:00.");
    } else {
        ESP_LOGD(logTag, "Time is already set.");
    }

    // Properly initialize some time vars
    {
        now = time(nullptr);
        if(irrigCtrlPersistentData.lastIrrigEvent == 0) {
            struct tm nowTm;
            localtime_r(&now, &nowTm);
            nowTm.tm_sec--;
            irrigCtrlPersistentData.lastIrrigEvent = mktime(&nowTm);
        }
    }

    // Register time system hook
    TimeSystem_RegisterHook(timeSytemEventsHookDispatch, this);

    while(1) {
        loopStartTicks = xTaskGetTickCount();

        // feed the emergency timer
        if(pdPASS != xTimerReset(emergencyTimerHandle, 10)) {
            ESP_LOGW(logTag, "Couldn't feed the emergency timer.");
        }

        // *********************
        // Power up needed peripherals, DCDC, ...
        // *********************
        // Peripheral enable will power up the DCDC as well as the RS232 driver
        if(!pwrMgr.getPeripheralEnable()) {
            ESP_LOGD(logTag, "Bringing up DCDC + RS232 driver.");
            pwrMgr.setPeripheralEnable(true);
            // Wait for stable onboard peripherals
            vTaskDelay(pdMS_TO_TICKS(peripheralEnStartupMillis));
        }

        // Enable external sensor power
        if((!disableReservoirCheck) && (!pwrMgr.getPeripheralExtSupply())) {
            ESP_LOGD(logTag, "Powering external sensors.");
            pwrMgr.setPeripheralExtSupply(true);
            // Wait for external sensors to power up properly
            vTaskDelay(pdMS_TO_TICKS(peripheralExtSupplyMillis));
        }

        // *********************
        // Fetch sensor data
        // *********************
        // Battery voltage
        state.battVoltage = pwrMgr.getSupplyVoltageMilli();
        if(disableBatteryCheck) {
            state.battState = PowerManager::BATT_DISABLED;
        } else {
            state.battState = pwrMgr.getBatteryState(state.battVoltage);
        }
        ESP_LOGD(logTag, "Battery voltage: %02.2f V (%s)", roundf(state.battVoltage * 0.1f) * 0.01f,
            BATT_STATE_TO_STR(state.battState));

        // Get fill level of the reservoir, if not disabled.
        if(!disableReservoirCheck) {
            state.fillLevel = fillSensor.getFillLevel(8, 100);
            state.reservoirState = irrigCtrlPersistentData.reservoirState; // keep previous state by default

            if ((irrigCtrlPersistentData.reservoirState == RESERVOIR_OK) ||
                (irrigCtrlPersistentData.reservoirState == RESERVOIR_DISABLED))
            {
                // state was okay or disabled before -> update it with the absolute values
                if(state.fillLevel >= fillLevelLowThresholdPercent10) {
                    state.reservoirState = RESERVOIR_OK;
                } else if (state.fillLevel >= fillLevelCriticalThresholdPercent10) {
                    state.reservoirState = RESERVOIR_LOW;
                } else {
                    state.reservoirState = RESERVOIR_CRITICAL;
                }
            } else {
                // apply appropriate hysteresis if we were critical or low before
                if (irrigCtrlPersistentData.reservoirState == RESERVOIR_CRITICAL) {
                    if(state.fillLevel >= (fillLevelLowThresholdPercent10 + fillLevelHysteresisPercent10)) {
                        state.reservoirState = RESERVOIR_OK;
                    } else if (state.fillLevel >= (fillLevelCriticalThresholdPercent10 + fillLevelHysteresisPercent10)) {
                       state.reservoirState = RESERVOIR_LOW;
                    }
                } else {
                    if(state.fillLevel >= (fillLevelLowThresholdPercent10 + fillLevelHysteresisPercent10)) {
                        state.reservoirState = RESERVOIR_OK;
                    } else if (state.fillLevel < fillLevelCriticalThresholdPercent10) {
                       state.reservoirState = RESERVOIR_CRITICAL;
                    }
                }
            }
        } else {
            state.fillLevel = -2;
            state.reservoirState = RESERVOIR_DISABLED;
        }
        ESP_LOGD(logTag, "Reservoir fill level: %d (%s)", state.fillLevel, 
            RESERVOIR_STATE_TO_STR(state.reservoirState));

        // Store updated fill values in persitent data storage
        irrigCtrlPersistentData.reservoirState = state.reservoirState;

        // Power down external supply already. Not needed anymore.
        if(pwrMgr.getPeripheralExtSupply()) {
            pwrMgr.setPeripheralExtSupply(false);
            ESP_LOGD(logTag, "Sensors powered down.");
        }

        // TBD: Get weather forecast
        // TBD: Get local weather data

        // Check system preconditions for the irrigation, i.e. battery state and reservoir fill level
        irrigOk = true;
        if(state.battState == PowerManager::BATT_CRITICAL) {
            irrigOk = false;
        }
        if(state.reservoirState == RESERVOIR_CRITICAL) {
            irrigOk = false;
        }

        // Check if system conditions got critical and outputs are active
        if(outputCtrl.anyOutputsActive() && !irrigOk) {
            ESP_LOGW(logTag, "Active outputs detected, but system conditions critical! Disabling them for safety.");
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
            events = xEventGroupClearBits(timeEvents, timeEventTimeSet | timeEventTimeSetSntp);
            if(0 != (events & timeEventTimeSet)) {
                ESP_LOGI(logTag, "Time set detected. Resetting event processing.");
                // Recalculate lastIrrigEvent to not process events that haven't really 
                // happend in-between last time and now.
                struct tm nowTm;
                localtime_r(&now, &nowTm);
                nowTm.tm_sec--;
                irrigCtrlPersistentData.lastIrrigEvent = mktime(&nowTm);

                // Calculate the next SNTP sync only if this was a manual time set event, otherwise
                // the next sync time has already been set above
                if(0 == (events & timeEventTimeSetSntp)) {
                    struct tm sntpNextSyncTm;
                    localtime_r(&now, &sntpNextSyncTm);
                    sntpNextSyncTm.tm_hour += sntpResyncIntervalHours;
                    sntpNextSync = mktime(&sntpNextSyncTm);
                    TimeSystem_SetNextSntpSync(sntpNextSync);
                }

                // Disable all outputs to abort any irrigations that may have been started.
                outputCtrl.disableAllOutputs();
            }

            nextIrrigEvent = irrigPlanner.getNextEventTime(irrigCtrlPersistentData.lastIrrigEvent, true);
            state.nextIrrigEvent = nextIrrigEvent;
            millisTillNextEvent = (int) round(difftime(nextIrrigEvent, now) * 1000.0);

            // // Publish state with the updated next event time
            // publishStateUpdate();

            // Perform event actions if it is time now
            now = time(nullptr);
            double diffTime = difftime(nextIrrigEvent, now);
            // Event within delta or have we even overshot the target?
            if((nextIrrigEvent != 0) && ((fabs(diffTime) < 1.0) || (diffTime <= -1.0))) {
                TimeSystem_LogTime();

                if(!irrigOk) {
                    ESP_LOGE(logTag, "Critical system conditions detected! Dropping irrigation.");
                }

                struct tm eventTm;
                localtime_r(&nextIrrigEvent, &eventTm);
                ESP_LOGI(logTag, "Actions to perform for events at %02d.%02d.%04d %02d:%02d:%02d",
                    eventTm.tm_mday, eventTm.tm_mon+1, 1900+eventTm.tm_year,
                    eventTm.tm_hour, eventTm.tm_min, eventTm.tm_sec);

                constexpr int maxEventHandles = 8; // TBD: config parameter
                IrrigationPlanner::event_handle_t eventHandles[maxEventHandles];

                plannerErr = irrigPlanner.getEventHandles(nextIrrigEvent, eventHandles, maxEventHandles);
                if(IrrigationPlanner::ERR_OK != plannerErr) {
                    ESP_LOGW(logTag, "Error getting event handles: %d. Trying our best anyway...", plannerErr);
                }
                for(int cnt=0; cnt < maxEventHandles; cnt++) {
                    IrrigationEvent::irrigation_event_data_t eventData;
                    if(eventHandles[cnt].idx >= 0) {
                        plannerErr = irrigPlanner.getEventData(eventHandles[cnt], &eventData);
                        if(IrrigationPlanner::ERR_OK != plannerErr) {
                            ESP_LOGE(logTag, "Error getting event data: %d. No actions available!", plannerErr);
                        } else {
                            irrigation_zone_cfg_t* zoneCfg = nullptr;
                            plannerErr = irrigPlanner.getZoneConfigPtr(eventData.zoneIdx, &zoneCfg);
                            if(IrrigationPlanner::ERR_OK != plannerErr) {
                                ESP_LOGE(caller->logTag, "Error getting zone config: %d. No actions available!", plannerErr);
                                zoneCfg = nullptr;
                            }

                            bool isStartEvent = eventData.isStart;
                            unsigned int durationSecs = isStartEvent ? eventData.durationSecs : 0;
                            if(nullptr != zoneCfg) {
                                for(int i=0; i < irrigationZoneCfgElements; i++) {
                                    if(zoneCfg->chEnabled[i]) {
                                        ESP_LOGI(logTag, "* Channel: %s, state: %s, duration: %d s, start: %d", 
                                        CH_MAP_TO_STR(zoneCfg->chNum[i]),
                                        isStartEvent ? (zoneCfg->chStateStart[i] ? "ON" : "OFF") :
                                                        (zoneCfg->chStateStop[i] ? "ON" : "OFF"),
                                        durationSecs, isStartEvent);
                                    }
                                }
                            }

                            plannerErr = irrigPlanner.confirmEvent(eventHandles[cnt]);
                            if(IrrigationPlanner::ERR_OK != plannerErr) {
                                ESP_LOGE(caller->logTag, "Error confirming event: %d. Not performing its actions!", plannerErr);
                            }

                            if((nullptr != zoneCfg) && (IrrigationPlanner::ERR_OK == plannerErr)) {
                                caller->setZoneOutputs(irrigOk, zoneCfg, isStartEvent);
                            }
                        }
                    } else {
                        break;
                    }
                }

                irrigCtrlPersistentData.lastIrrigEvent = nextIrrigEvent;
            } else {
                eventsToProcess = false;
            }

            // Publish state with the updated next event time + active outputs
            state.sntpLastSync = TimeSystem_GetLastSntpSync();
            state.sntpNextSync = TimeSystem_GetNextSntpSync();
            publishStateUpdate();
        }

        // *********************
        // SNTP resync
        // *********************
        // Request an SNTP resync if it hasn't happend at all or the next scheduled sync is due
        sntpNextSync = TimeSystem_GetNextSntpSync();
        if((sntpNextSync == 0) || (difftime(sntpNextSync, time(nullptr)) <= 0.0f)) {
            bool skipSntpResync = false;

            // Skip resync in case an upcoming event is close
            millisTillNextEvent = (int) round(difftime(nextIrrigEvent, time(nullptr)) * 1000.0);
            if(millisTillNextEvent <= noSntpResyncRangeMillis) {
                skipSntpResync = true;
                ESP_LOGD(logTag, "Skipping SNTP (re)sync, because next upcoming event is too close.");
            }

            // Skip rsync if we are offline
            events = xEventGroupWaitBits(wifiEvents, wifiEventConnected, pdFALSE, pdTRUE, 0);
            if (0 == (events & wifiEventConnected)) {
                skipSntpResync = true;
                ESP_LOGD(logTag, "Skipping SNTP (re)sync, because we are offline.");
            }

            // Skip resync if outputs are active, because the new time being set would disable
            // all outputs and they would be turned back on again
            if(outputCtrl.anyOutputsActive()) {
                skipSntpResync = true;
                ESP_LOGD(logTag, "Skipping SNTP (re)sync, because outputs are active.");
            }

            if(!skipSntpResync) {
                ESP_LOGI(logTag, "Requesting an SNTP time (re)sync.");
                TimeSystem_SntpRequest();

                // Wait for the sync
                events = xEventGroupWaitBits(timeEvents, timeEventTimeSetSntp, pdTRUE, pdTRUE, pdMS_TO_TICKS(timeResyncWaitMillis));

                // Stop the SNTP background process, so it won't intefere with running irrigations
                TimeSystem_SntpStop();

                // Calculate the next SNTP sync
                struct tm sntpNextSyncTm;
                sntpNextSync = time(nullptr);
                localtime_r(&sntpNextSync, &sntpNextSyncTm);
                // Check status of sync to determine the next sync time
                if(0 != (events & timeEventTimeSetSntp)) {
                    ESP_LOGI(logTag, "SNTP time (re)sync was successful.");
                    sntpNextSyncTm.tm_hour += sntpResyncIntervalHours;
                } else {
                    ESP_LOGW(logTag, "SNTP time (re)sync wasn't successful within timeout.");
                    sntpNextSyncTm.tm_min += sntpResyncIntervalFailMinutes;
                }
                sntpNextSync = mktime(&sntpNextSyncTm);
                TimeSystem_SetNextSntpSync(sntpNextSync);

                // Update SNTP info
                state.sntpLastSync = TimeSystem_GetLastSntpSync();
                state.sntpNextSync = TimeSystem_GetNextSntpSync();
            }
        }

        // *********************
        // Sleep preparation
        // *********************
        // Get current time again for sleep calculation
        now = time(nullptr);

        // Check if the system time has been (re)set
        events = xEventGroupClearBits(timeEvents, timeEventTimeSet | timeEventTimeSetSntp);
        if(0 != (events & timeEventTimeSet)) {
            // Recalculate lastIrrigEvent and nextIrrig to not process events that haven't really 
            // happend in-between last time and now.
            ESP_LOGI(logTag, "Time set detected. Resetting event processing.");
            struct tm nowTm;
            localtime_r(&now, &nowTm);
            nowTm.tm_sec--;
            irrigCtrlPersistentData.lastIrrigEvent = mktime(&nowTm);
            nextIrrigEvent = irrigPlanner.getNextEventTime(irrigCtrlPersistentData.lastIrrigEvent, true);

            // Calculate the next SNTP sync only if this was a manual time set event, otherwise
            // the next sync time has already been set above
            if(0 == (events & timeEventTimeSetSntp)) {
                struct tm sntpNextSyncTm;
                localtime_r(&now, &sntpNextSyncTm);
                sntpNextSyncTm.tm_hour += sntpResyncIntervalHours;
                sntpNextSync = mktime(&sntpNextSyncTm);
                TimeSystem_SetNextSntpSync(sntpNextSync);
            }

            // Disable all outputs to abort any irrigations that may have been started.
            outputCtrl.disableAllOutputs();

            // Update next irrigation event and publish (also the new SNTP info set above)
            state.nextIrrigEvent = nextIrrigEvent;
            publishStateUpdate();
        }

        millisTillNextEvent = (int) round(difftime(nextIrrigEvent, now) * 1000.0);

        // Power down the DCDC if no outputs are active.
        if(!outputCtrl.anyOutputsActive()) {
            pwrMgr.setPeripheralEnable(false);
            ESP_LOGD(logTag, "DCDC + RS232 driver powered down.");
        }

        if(pwrMgr.getKeepAwake()) {
            // Calculate loop runtime and compensate the sleep time with it
            nowTicks = xTaskGetTickCount();
            int loopRunTimeMillis = portTICK_RATE_MS * ((nowTicks > loopStartTicks) ? 
                (nowTicks - loopStartTicks) :
                (portMAX_DELAY - loopStartTicks + nowTicks + 1));
            ESP_LOGD(logTag, "Loop runtime %d ms.", loopRunTimeMillis);
            firstRun = false;

            int sleepMillis = wakeupIntervalKeepAwakeMillis - loopRunTimeMillis;
            if(sleepMillis > millisTillNextEvent) sleepMillis = millisTillNextEvent - preEventMillis;
            if(sleepMillis < 500) sleepMillis = 500;

            ESP_LOGD(logTag, "Task is going to sleep for %d ms.", sleepMillis);
            if(sleepMillis > taskMaxSleepTimeMillis) {
                sleepMillis = taskMaxSleepTimeMillis;
                ESP_LOGD(logTag, "Task sleep time longer than maximum allowed. "
                    "Task is going to sleep for %d ms insted.", sleepMillis);
            }
            vTaskDelay(pdMS_TO_TICKS(sleepMillis));
        } else {
            // Wait to get all updates through
            if(!mqttMgr.waitAllPublished(mqttAllPublishedWaitMillis)) {
                ESP_LOGW(logTag, "Waiting for MQTT to publish all messages didn't complete within timeout.");
            }

            // TBD: stop webserver, mqtt and other stuff

            // Calculate loop runtime and compensate the sleep time with it
            int loopRunTimeMillis;
            if(firstRun) {
                // This is the normal case for deep sleep: Compensate also for the
                // boot time.
                loopRunTimeMillis = portTICK_RATE_MS * xTaskGetTickCount();
                firstRun = false;
                ESP_LOGD(logTag, "Loop runtime (incl. boot) %d ms.", loopRunTimeMillis);
            } else {
                // This is the case when the system was in keep awake, previously. So,
                // compensate for the loop time only.
                nowTicks = xTaskGetTickCount();
                loopRunTimeMillis = portTICK_RATE_MS * ((nowTicks > loopStartTicks) ? 
                    (nowTicks - loopStartTicks) :
                    (portMAX_DELAY - loopStartTicks + nowTicks + 1));
                ESP_LOGD(logTag, "Loop runtime %d ms.", loopRunTimeMillis);
            }

            int millisTillNextEventCompensated = millisTillNextEvent - preEventMillisDeepSleep - mqttAllPublishedWaitMillis;
            int sleepMillis = wakeupIntervalMillis - loopRunTimeMillis;
            if(sleepMillis > millisTillNextEventCompensated) sleepMillis = millisTillNextEventCompensated;
            if(sleepMillis < 500) sleepMillis = 500;

            // Check if there is enough wakeup time
            if( (sleepMillis < noDeepSleepRangeMillis) && 
                (millisTillNextEvent <= noDeepSleepRangeMillis) ) {
                ESP_LOGD(logTag, "Event coming up sooner than deep sleep wakeup time. "
                    "Task is going to sleep for %d ms insted of deep sleep.", sleepMillis);
                if(sleepMillis > taskMaxSleepTimeMillis) {
                    sleepMillis = taskMaxSleepTimeMillis;
                    ESP_LOGD(logTag, "Task sleep time is bigger than maximum allowed. "
                        "Task is going to sleep for %d ms insted.", sleepMillis);
                }
                vTaskDelay(pdMS_TO_TICKS(sleepMillis));
            }
            // Check if any outputs are active, deep sleep would kill them!
            else if(outputCtrl.anyOutputsActive()) {
                ESP_LOGD(logTag, "Outputs active. Task is going to sleep for %d ms insted of deep sleep.", sleepMillis);
                if(sleepMillis > taskMaxSleepTimeMillis) {
                    sleepMillis = taskMaxSleepTimeMillis;
                    ESP_LOGD(logTag, "Task sleep time is bigger than maximum allowed. "
                        "Task is going to sleep for %d ms insted.", sleepMillis);
                }
                vTaskDelay(pdMS_TO_TICKS(sleepMillis));
            }
            else {
                TickType_t killStartTicks = xTaskGetTickCount();

                ESP_LOGD(logTag, "About to deep sleep. Killing MQTT and WiFi.");
                mqttMgr.stop();
                // don't stop WiFi explicitly, because this seemed to hang sometimes.

                nowTicks = xTaskGetTickCount();
                loopRunTimeMillis = portTICK_RATE_MS * ((nowTicks > killStartTicks) ? 
                    (nowTicks - killStartTicks) :
                    (portMAX_DELAY - killStartTicks + nowTicks + 1));

                sleepMillis -= loopRunTimeMillis;
                ESP_LOGD(logTag, "Kill compensation time %d ms; new deep sleep time %d ms.", \
                    loopRunTimeMillis, sleepMillis);

                if(sleepMillis < noDeepSleepRangeMillis) {
                    ESP_LOGW(logTag, "Compensating deep sleep time got too near to next event. Rebooting.");
                    pwrMgr.reboot();
                } else {
                    ESP_LOGD(logTag, "Preparing deep sleep for %d ms.", sleepMillis);
                    pwrMgr.gotoSleep(sleepMillis);
                }
            }
        }
    }

    vTaskDelete(NULL);

    ESP_LOGE(logTag, "Task unexpectetly exited! Performing reboot.");
    //pwrMgr->reboot();
}

void IrrigationController::setZoneOutputs(bool irrigOk, irrigation_zone_cfg_t* zoneCfg, bool start)
{
    for(int i=0; i < irrigationZoneCfgElements; i++) {
        if(zoneCfg->chEnabled[i]) {
            bool switchOn = start ? zoneCfg->chStateStart[i] : zoneCfg->chStateStop[i];
            OutputController::ch_map_t chNum = zoneCfg->chNum[i];
            // Only enable outputs when preconditions are met; disabling is always okay.
            if(irrigOk || !switchOn) {
                outputCtrl.setOutput(chNum, switchOn);
                updateStateActiveOutputs(chNum, switchOn);
            }
        }
    }
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
void IrrigationController::publishStateUpdate()
{
    static uint8_t mac_addr[6];
    static char timeStr[20];
    static char sntpLastSyncTimeStr[20];
    static char sntpNextSyncTimeStr[20];
    static char activeOutputs[4*(OutputController::intChannels+OutputController::extChannels)+1];
    static char activeOutputsStr[8*(OutputController::intChannels+OutputController::extChannels)+1];
    size_t preLen;
    size_t postLen;

    // TBD: Compare more lax, i.e. allow little differences in batt voltage, etc.
    int stateCmp = memcmp(&state, &lastState, sizeof(state_t));

    if(0 != stateCmp) {
        if(false == mqttMgr.waitConnected(mqttConnectedWaitMillis)) {
            ESP_LOGW(logTag, "MQTT manager has no connection after timeout.");
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
                mqttMgr.publish(mqttStateTopic, mqttStateData, actualLen, MqttManager::QOS_EXACTLY_ONCE, true);

                // Copy the sent state over to lastState, but only if we actually sent it and not in the other cases
                memcpy(&lastState, &state, sizeof(state_t));
            }
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
        ESP_LOGE("unkown", "No valid IrrigationController available to dispatch time system events to!");
    } else {
        if(0 != events) {
            controller->timeSytemEventHandler(events);
        }
    }
}


/**
 * @brief Emergency reboot timer callback, which will simply reset the device
 * to get back up in operational state.
 * 
 * @param timerHandle Timer handle for identification, unused
 */
void IrrigationController::emergencyTimerCb(TimerHandle_t timerHandle)
{
    // hardcore reboot, without accessing the power manager in case something
    // really bad happend.
    esp_restart_noos();
}
