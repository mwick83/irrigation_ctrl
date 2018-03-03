#include "irrigationController.h"

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
    // 19 digits for the next event datetime.
    // Format string is a bit too long, but don't care too mich about those few bytes
    mqttStateDataMaxLen = strlen(mqttStateDataFmt) + 5 + 1 + 8 + 4 + 1 + 8 + 19 + 1;
    mqttStateData = (char*) calloc(mqttStateDataMaxLen, sizeof(char));

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
    TickType_t wait, loopStartTicks;
    time_t now, nextIrrigEvent, lastIrrigEvent;

    // Wait for WiFi to come up. TBD: make configurable (globally), implement WiFiManager for that
    wait = portMAX_DELAY;
    if(caller->wifiConnectedWaitMillis >= 0) {
        wait = pdMS_TO_TICKS(caller->wifiConnectedWaitMillis);
    }
    events = xEventGroupWaitBits(wifiEvents, wifiEventConnected, 0, pdTRUE, wait);
    if(0 != (events & wifiEventConnected)) {
        ESP_LOGD(caller->logTag, "WiFi connected.");
    } else {
        ESP_LOGE(caller->logTag, "WiFi didn't come up with timeout!");
    }

    // Wait for the time system to come up. This means either SNTP was successful or the
    // time was available in the RTC.
    // TBD: How to achieve periodic SNTP resyncs, as there is currently no way to wait for
    // a successful SNTP sync within the API.
    if(!TimeSystem_WaitTimeSet(caller->timeSetWaitMillis)) {
        // Time hasen't been set within timeout, so assume something to get operating somehow.
        // After setting it here, it will be stored in the RTC, so next time we come up it
        // will not be set to the default again.
        TimeSystem_SetTime(01, 01, 2018, 06, 00, 00);
        ESP_LOGW(caller->logTag, "Time hasn't been set within timeout! Setting default time: 2018-01-01, 06:00:00.");
    } else {
        ESP_LOGD(caller->logTag, "Got time.");
    }

    // Properly initialize some time vars
    {
        now = time(nullptr);
        struct tm nowTm;
        localtime_r(&now, &nowTm);
        nowTm.tm_sec--;
        lastIrrigEvent = mktime(&nowTm);
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
            // Wait for stable DCDC: According to datasheet soft-start is 2.1ms
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        // Enable external sensor power
        if(!pwrMgr.getPeripheralExtSupply()) {
            ESP_LOGD(caller->logTag, "Powering external sensors.");
            pwrMgr.setPeripheralExtSupply(true);
            // Wait a bit more for external sensors to power up properly
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        // *********************
        // Fetch sensor data
        // *********************
        // Battery voltage
        caller->state.battVoltage = pwrMgr.getSupplyVoltageMilli();
        caller->state.battState = pwrMgr.getBatteryState(caller->state.battVoltage);
        ESP_LOGD(caller->logTag, "Batt voltage: %02.2f V (%s)", roundf(caller->state.battVoltage * 0.1f) * 0.01f,
            BATT_STATE_TO_STR(caller->state.battState));

        // Fill level of the reservoir
        caller->state.fillLevel = fillSensor.getFillLevel();
        if(caller->state.fillLevel >= fillLevelLowThreshold) {
            caller->state.reservoirState = RESERVOIR_OK;
        } else if (caller->state.fillLevel >= fillLevelCriticalThreshold) {
            caller->state.reservoirState = RESERVOIR_LOW;
        } else {
            caller->state.reservoirState = RESERVOIR_CRITICAL;
        }
        ESP_LOGD(caller->logTag, "Fill level: %d (%s)", caller->state.fillLevel, 
            RESERVOIR_STATE_TO_STR(caller->state.reservoirState));

        // Power down external supply already. Not needed anymore.
        pwrMgr.setPeripheralExtSupply(false);
        ESP_LOGD(caller->logTag, "Sensors powered down.");

        // TBD: Get weather forecast
        // TBD: Get local weather data

        // *********************
        // Irrigation
        // *********************
        int millisTillNextEvent;
        bool eventsToProcess = true;
        while(eventsToProcess) {
            now = time(nullptr);

            // Check if the system time has been (re)set
            if(0 != (xEventGroupClearBits(caller->timeEvents, caller->timeEventTimeSet) & caller->timeEventTimeSet)) {
                ESP_LOGI(caller->logTag, "Time set detected. Resetting event processing.");
                // Recalculate lastIrrigEvent to not process events that haven't really 
                // happend in-between last time and now.
                struct tm nowTm;
                localtime_r(&now, &nowTm);
                nowTm.tm_sec--;
                lastIrrigEvent = mktime(&nowTm);

                // Disable all outputs to abort any irrigations that may have been started.
                outputCtrl.disableAllOutputs();
            }

            nextIrrigEvent = irrigPlanner.getNextEventTime(lastIrrigEvent, true);
            caller->state.nextIrrigEvent = nextIrrigEvent;
            millisTillNextEvent = (int) round(difftime(nextIrrigEvent, now) * 1000.0);

            // Publish state with the updated next event time
            caller->publishStateUpdate();

            // Perform event actions if it is time now
            now = time(nullptr);
            double diffTime = difftime(nextIrrigEvent, now);
            // Event within delta or have we even overshot the target?
            if((nextIrrigEvent != 0) && ((fabs(diffTime) < 1.0) || (diffTime <= -1.0))) {
                TimeSystem_LogTime();

                // Check preconditions for the irrigation, i.e. battery state and reservoir fill level
                bool irrigOk = true;
                if(caller->state.battState == PowerManager::BATT_CRITICAL) {
                    ESP_LOGE(caller->logTag, "Battery state is critical! Dropping irrigation.");
                    irrigOk = false;
                }
                if(caller->state.reservoirState == RESERVOIR_CRITICAL) {
                    ESP_LOGE(caller->logTag, "Reservoir fill level is critical! Dropping irrigation.");
                    irrigOk = false;
                }

                struct tm eventTm;
                localtime_r(&nextIrrigEvent, &eventTm);
                ESP_LOGI(caller->logTag, "Actions to perform for event at %02d.%02d.%04d %02d:%02d:%02d",
                    eventTm.tm_mday, eventTm.tm_mon+1, 1900+eventTm.tm_year,
                    eventTm.tm_hour, eventTm.tm_min, eventTm.tm_sec);

                std::vector<IrrigationEvent::ch_cfg_t> chCfg;
                irrigPlanner.getEventChannelConfig(nextIrrigEvent, &chCfg);
                for(std::vector<IrrigationEvent::ch_cfg_t>::iterator chIt = chCfg.begin(); chIt != chCfg.end(); chIt++) {
                    ESP_LOGI(caller->logTag, "  * Channel: %s, state: %s", 
                        CH_MAP_TO_STR((*chIt).chNum), (*chIt).switchOn ? "ON" : "OFF");

                    // Only enable outputs when preconditions are met; disabling is always okay.
                    if(irrigOk || !(*chIt).switchOn) {
                        outputCtrl.setOutput((OutputController::ch_map_t) (*chIt).chNum, (*chIt).switchOn);
                    }
                }

                lastIrrigEvent = nextIrrigEvent;
            } else {
                eventsToProcess = false;
            }
        }

        // *********************
        // Sleeping
        // *********************
        // Get current time again for sleep calculation
        now = time(nullptr);

        // Check if the system time has been (re)set
        if(0 != (xEventGroupClearBits(caller->timeEvents, caller->timeEventTimeSet) & caller->timeEventTimeSet)) {
            // Recalculate lastIrrigEvent and nextIrrig to not process events that haven't really 
            // happend in-between last time and now.
            ESP_LOGI(caller->logTag, "Time set detected. Resetting event processing.");
            struct tm nowTm;
            localtime_r(&now, &nowTm);
            nowTm.tm_sec--;
            lastIrrigEvent = mktime(&nowTm);
            nextIrrigEvent = irrigPlanner.getNextEventTime(lastIrrigEvent, true);

            // Disable all outputs to abort any irrigations that may have been started.
            outputCtrl.disableAllOutputs();
        }

        millisTillNextEvent = (int) round(difftime(nextIrrigEvent, now) * 1000.0);

        // Power down the DCDC if no outputs are active.
        if(!outputCtrl.anyOutputsActive()) {
            pwrMgr.setPeripheralEnable(false);
            ESP_LOGD(caller->logTag, "DCDC + RS232 driver powered down.");
        }

        if(pwrMgr.getKeepAwake()) {
            // Calculate loop runtime and compensate the sleep time with it
            TickType_t nowTicks = xTaskGetTickCount();
            int loopRunTimeMillis = portTICK_RATE_MS * ((nowTicks > loopStartTicks) ? 
                (nowTicks - loopStartTicks) :
                (portMAX_DELAY - loopStartTicks + nowTicks + 1));
            ESP_LOGD(caller->logTag, "Loop runtime %d ms.", loopRunTimeMillis);

            int sleepMillis = caller->wakeupIntervalKeepAwakeMillis - loopRunTimeMillis;
            if(sleepMillis > millisTillNextEvent) sleepMillis = millisTillNextEvent - caller->preEventMillis;
            if(sleepMillis < 50) sleepMillis = 50;

            ESP_LOGD(caller->logTag, "Task is going to sleep for %d ms.", sleepMillis);
            vTaskDelay(pdMS_TO_TICKS(sleepMillis));
        } else {
            // Wait to get all updates through
            if(!mqttMgr.waitAllPublished(caller->mqttAllPublishedWaitMillis)) {
                ESP_LOGW(caller->logTag, "Waiting for MQTT to publish all messages didn't complete within timeout.");
            }

            // TBD: stop webserver, mqtt and other stuff

            // Calculate loop runtime and compensate the sleep time with it
            TickType_t nowTicks = xTaskGetTickCount();
            int loopRunTimeMillis = portTICK_RATE_MS * ((nowTicks > loopStartTicks) ? 
                (nowTicks - loopStartTicks) :
                (portMAX_DELAY - loopStartTicks + nowTicks + 1));
            ESP_LOGD(caller->logTag, "Loop runtime %d ms.", loopRunTimeMillis);

            int sleepMillis = caller->wakeupIntervalMillis - loopRunTimeMillis;
            if(sleepMillis > millisTillNextEvent) sleepMillis = millisTillNextEvent - caller->preEventMillis;
            if(sleepMillis < 50) sleepMillis = 50;

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
 * @brief Publish currently stored state via MQTT.
 */
void IrrigationController::publishStateUpdate(void)
{
    static uint8_t mac_addr[6];
    static char timeStr[20];
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
            time_t nextIrrigEvent = state.nextIrrigEvent;
            struct tm nextIrrigEventTm;
            localtime_r(&nextIrrigEvent, &nextIrrigEventTm);
            strftime(timeStr, 20, "%Y-%m-%d %H:%M:%S", &nextIrrigEventTm);

            size_t actualLen = snprintf(mqttStateData, mqttStateDataMaxLen, mqttStateDataFmt,
                state.battVoltage, state.battState, BATT_STATE_TO_STR(state.battState), 
                state.fillLevel, state.reservoirState, RESERVOIR_STATE_TO_STR(state.reservoirState),
                timeStr);
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
void IrrigationController::timeSytemEventTimeSet(void)
{
    // set an event group bit for the processing task
    xEventGroupSetBits(timeEvents, timeEventTimeSet);
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
void IrrigationController::timeSytemEventsHookDispatch(void* param, time_system_event_t event)
{
    IrrigationController* controller = (IrrigationController*) param;

    if(nullptr == controller) {
        ESP_LOGE("unkown", "No valid IrrigationController available to dispatch time system events to!")
    } else {
        if(event == TIMESYSTEM_TIME_SET) {
            controller->timeSytemEventTimeSet();
        }
    }
}
