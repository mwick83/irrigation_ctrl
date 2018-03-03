#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <stdint.h>
#include <cmath> // used for NAN

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_sleep.h"
#include "esp_log.h"

#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "driver/gpio.h"

#include "user_config.h"

#define BATT_STATE_TO_STR(state) (\
    (state == PowerManager::BATT_FULL) ? "FULL" : \
    (state == PowerManager::BATT_OK) ? "OK" : \
    (state == PowerManager::BATT_LOW) ? "LOW" : \
    (state == PowerManager::BATT_CRITICAL) ? "CRITICAL" : \
    "UNKOWN" \
)

class PowerManager
{
private:
    const char* logTag = "pwr_mgr";

    //const uint32_t adcVref = 1157;
    const uint32_t adcVref = 1152;
    float battVoltageMult;
    esp_adc_cal_characteristics_t battVoltageAdcCharacteristics;

    SemaphoreHandle_t peripheralEnMutex;
    StaticSemaphore_t peripheralEnMutexBuf;
    bool peripheralEnState;

    SemaphoreHandle_t peripheralExtSupplyMutex;
    StaticSemaphore_t peripheralExtSupplyMutexBuf;
    bool peripheralExtSupplyState;

    bool keepAwakeForcedState;
    bool keepAwakeAtBootState;

public:
    typedef enum {
        BATT_FULL = 0,
        BATT_OK = 1,
        BATT_LOW = 2,
        BATT_CRITICAL = 3,
    } batt_state_t;

    PowerManager(void);

    uint32_t getSupplyVoltageMilli(void);
    batt_state_t getBatteryState(uint32_t millis);

    void setPeripheralEnable(bool en);
    bool getPeripheralEnable(void);
    void setPeripheralExtSupply(bool en);
    bool getPeripheralExtSupply(void);

    bool getKeepAwake(void);
    void setKeepAwakeForce(bool en);
    bool getKeepAwakeForce(void);
    bool getKeepAwakeIo(void);
    bool getKeepAwakeAtBoot(void);

    bool gotoSleep(uint32_t ms);
};

#endif /* POWER_MANAGER_H */
