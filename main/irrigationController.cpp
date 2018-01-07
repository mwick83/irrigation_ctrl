#include "irrigationController.h"

IrrigationController::IrrigationController(void)
{
}

void IrrigationController::start(void)
{
    taskHandle = xTaskCreateStatic(taskFunc, "irrig_ctrl_task", taskStackSize, (void*) this, taskPrio, taskStack, &taskBuf);
    if(NULL != taskHandle) {
        ESP_LOGI(logTag, "IrrigationController task created. Starting.");
    } else {
        ESP_LOGE(logTag, "IrrigationController task creation failed!");
    }
}

void IrrigationController::taskFunc(void* params)
{
    IrrigationController const* caller = (IrrigationController*) params;

    EventBits_t events;
    TickType_t wait;

    int fillLevel;
    float battVoltage;

    // Wait for WiFi to come up. TBD: make configurable (globally), implement WiFiManager for that
    wait = portMAX_DELAY;
    if(caller->wifiConnecntedWaitMillis >= 0) {
        wait = pdMS_TO_TICKS(caller->wifiConnecntedWaitMillis);
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
        // *********************
        // Power up needed peripheals, DCDC, ...
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
        battVoltage = pwrMgr.getSupplyVoltageMilli();
        ESP_LOGD(caller->logTag, "Batt voltage: %02.2f V", roundf(battVoltage * 0.1f) * 0.01f);

        // Fill level of the reservoir
        fillLevel = fillSensor.getFillLevel();
        ESP_LOGD(caller->logTag, "Fill level: %d", fillLevel);

        // Power down external supply already. Not needed anymore.
        pwrMgr.setPeripheralExtSupply(false);
        ESP_LOGD(caller->logTag, "Sensors powered down.");

        // TBD: Get weather forecast
        // TBD: Get local weather data

        // *********************
        // Irrigation
        // *********************
        // Get irrigation plan
        // Update state

        // Perform the irrigation

        // Power down the DCDC. Irrigation is done.
        pwrMgr.setPeripheralEnable(false);
        ESP_LOGD(caller->logTag, "DCDC + RS232 driver powered down.");

        // Update state again on finish

        // Wait to get all updates through

        // *********************
        // Sleeping
        // *********************
        // TBD: Use irrigation time for wakeup calculation
        // TBD: Use boot time / last loop run for wakeup calculation
        if(pwrMgr.getKeepAwake()) {
            ESP_LOGD(caller->logTag, "Task is going to sleep.");
            vTaskDelay(pdMS_TO_TICKS(caller->wakeupIntervalKeepAwakeMillis));
        } else {
            ESP_LOGD(caller->logTag, "Preparing deep sleep.");
            // TBD: stop webserver, mqtt and other stuff
            pwrMgr.gotoSleep(caller->wakeupIntervalMillis);
        }
    }

    vTaskDelete(NULL);

    ESP_LOGE(caller->logTag, "Task unexpectetly exited! Performing reboot.");
    //pwrMgr->reboot();
}
