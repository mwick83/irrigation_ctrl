#include "irrigationController.h"

/**
 * @brief Default constructor, which performs basic initialization,
 * but doesn't start processing.
 */
IrrigationController::IrrigationController(void)
{
    size_t len = strlen(mqttTopicPre) + strlen(mqttStateTopicPost) + 12 + 1;
    mqttStateTopic = (char*) calloc(len, sizeof(char));
    
    // Format string length + 5 digits volatage in mV + 4 digits (fillLevel * 10) + 19 digits for the next event datetime
    // Format string is a bit too long, but don't care too mich about those few bytes
    mqttStateDataMaxLen = strlen(mqttStateDataFmt) + 5 + 4 + 19 + 1;
    mqttStateData = (char*) calloc(mqttStateDataMaxLen, sizeof(char));
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
    TickType_t wait;
    time_t now, nextIrrigEvent, loopStartTime;

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
        TimeSystem_SetTime(01, 01, 2018, 07, 00, 00);
        ESP_LOGW(caller->logTag, "Time hasn't been set within timeout! Setting default time: 2018-01-01, 07:00:00.");
    } else {
        ESP_LOGD(caller->logTag, "Got time.");
    }

    while(1) {
        loopStartTime = time(nullptr);

        // *********************
        // Power up needed peripherals, DCDC, ...
        // *********************
        // Peripheral enable will power up the DCDC as well as the RS232 driver
        ESP_LOGD(caller->logTag, "Bringing up DCDC + RS232 driver.");
        pwrMgr.setPeripheralEnable(true);
        // Wait for stable DCDC: According to datasheet soft-start is 2.1ms
        vTaskDelay(pdMS_TO_TICKS(5));
        // Enable external sensor power
        ESP_LOGD(caller->logTag, "Powering external sensors.");
        pwrMgr.setPeripheralExtSupply(true);
        // Wait a bit more for external sensors to power up properly
        vTaskDelay(pdMS_TO_TICKS(100));

        // *********************
        // Fetch sensor data
        // *********************
        // Battery voltage
        caller->state.battVoltage = pwrMgr.getSupplyVoltageMilli();
        ESP_LOGD(caller->logTag, "Batt voltage: %02.2f V", roundf(caller->state.battVoltage * 0.1f) * 0.01f);

        // Fill level of the reservoir
        caller->state.fillLevel = fillSensor.getFillLevel();
        ESP_LOGD(caller->logTag, "Fill level: %d", caller->state.fillLevel);

        // Power down external supply already. Not needed anymore.
        pwrMgr.setPeripheralExtSupply(false);
        ESP_LOGD(caller->logTag, "Sensors powered down.");

        // TBD: Get weather forecast
        // TBD: Get local weather data

        // *********************
        // Irrigation
        // *********************
        now = time(nullptr);
        nextIrrigEvent = irrigPlanner.getNextEventTime();
        caller->state.nextIrrigEvent = nextIrrigEvent;

        // Perform event actions if it is time now
        if((nextIrrigEvent != 0) && (fabs(difftime(nextIrrigEvent, now)) < 0.1)) {
            // TBD: get all events + their configs
            ESP_LOGD(caller->logTag, "TBD: Events shall happen now!");
        }

        // Publish state
        caller->publishStateUpdate();

        // TBD: Check if any outputs are active
        // Power down the DCDC. Irrigation is done.
        pwrMgr.setPeripheralEnable(false);
        ESP_LOGD(caller->logTag, "DCDC + RS232 driver powered down.");

        // *********************
        // Sleeping
        // *********************
        // get next event time and current time again for sleep calculation
        nextIrrigEvent = irrigPlanner.getNextEventTime();
        now = time(nullptr);

        if((nextIrrigEvent != 0) && (difftime(nextIrrigEvent, now) <= 60.0)) {
            ESP_LOGD(caller->logTag, "Next irrigation event is coming up soon: Task is going to sleep.");
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            if(pwrMgr.getKeepAwake()) {
                int loopRunTimeMillis = (int) round(difftime(now, loopStartTime) * 1000.0);
                ESP_LOGD(caller->logTag, "Loop runtime %d ms.", loopRunTimeMillis);

                int delayMillis = caller->wakeupIntervalKeepAwakeMillis - loopRunTimeMillis;
                if(delayMillis < 100) delayMillis = 100;

                ESP_LOGD(caller->logTag, "Task is going to sleep for %d ms.", delayMillis);
                vTaskDelay(pdMS_TO_TICKS(delayMillis));
            } else {
                // Wait to get all updates through
                if(!mqttMgr.waitAllPublished(caller->mqttAllPublishedWaitMillis)) {
                    ESP_LOGW(caller->logTag, "Waiting for MQTT to publish all messages didn't complete within timeout.");
                }

                // TBD: stop webserver, mqtt and other stuff

                // The waiting above may have taken some time, so get current time again
                now = time(nullptr);
                int loopRunTimeMillis = (int) round(difftime(now, loopStartTime) * 1000.0);
                ESP_LOGD(caller->logTag, "Loop runtime %d ms.", loopRunTimeMillis);

                int sleepMillis = caller->wakeupIntervalMillis - loopRunTimeMillis;
                if(sleepMillis < 100) sleepMillis = 100;

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

            size_t actualLen = snprintf(mqttStateData, mqttStateDataMaxLen, mqttStateDataFmt, state.battVoltage, state.fillLevel, timeStr);
            mqttMgr.publish(mqttStateTopic, mqttStateData, actualLen, MqttManager::QOS_EXACTLY_ONCE, false);
        }
    }
}
