#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <cstdint>
#include <cmath> // used for NAN

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_sleep.h"
#include "esp_log.h"

#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "driver/gpio.h"

#include "user_config.h"


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
    PowerManager(void);

    float getSupplyVoltageMilli(void);

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
