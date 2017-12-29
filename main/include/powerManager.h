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


// TBD: how to encapsulate this correctly?
static const char* LOG_TAG_POWER_MANAGER __attribute__((unused)) = "pwr_mgr";


class PowerManager
{
private:
    const uint32_t adcVref = 1157;
    float battVoltageMult;
    esp_adc_cal_characteristics_t battVoltageAdcCharacteristics;

    bool peripheralEnState;
    bool peripheralExtSupplyState;

    const uint64_t deepSleepTimeUs = 5000000;

public:
    PowerManager(void);

    float getSupplyVoltageMilli(void);

    void setPeripheralEnable(bool en);
    bool getPeripheralEnable(void);
    void setPeripheralExtSupply(bool en);
    bool getPeripheralExtSupply(void);

    bool getKeepAwake(void);
    bool gotoSleep(void);
};

#endif /* POWER_MANAGER_H */
