#include "powerManager.h"


PowerManager::PowerManager(void)
{
    // setup ADC for battery voltage conversion
    adc1_config_width(ADC_WIDTH_12Bit);
    adc1_config_channel_atten(battVoltageChannel, ADC_ATTEN_11db);

    // get calibration characteristics for the battery voltage channel
    // TBD: use v_ref from efuse once the API has support for it
    esp_adc_cal_get_characteristics(adcVref, ADC_ATTEN_11db, ADC_WIDTH_12Bit, &battVoltageAdcCharacteristics);

    // Multiplication and division settings:
    // - The external divider divides by 10.1, but the output function outputs millivolts
    // - Attenuation will be set to 11db (to have fullscale value of 3.9V; max input is limited to 3.3V)
    // - ADC is set to convert with 12bit precision
    // 4095 -> 3.9V * 10.1 -> Result 39390
    //battVoltageMult = 9.61904761905F;
    // The calibration functions outputs mV, so the factor is now the external divider alone:
    battVoltageMult = 10.1F;

    // setup peripheral power and enable GPIOs
    gpio_set_level(peripheralEnGpioNum, 0);
    gpio_set_level(peripheralExtSupplyGpioNum, 0);
    gpio_set_direction(peripheralEnGpioNum, GPIO_MODE_OUTPUT);
    gpio_set_direction(peripheralExtSupplyGpioNum, GPIO_MODE_OUTPUT);

    peripheralEnState = false;
    peripheralExtSupplyState = false;

    // setup keep awake input
    gpio_set_direction(keepAwakeGpioNum, GPIO_MODE_INPUT);
    gpio_set_pull_mode(keepAwakeGpioNum, GPIO_FLOATING); // board has an external pull
}

float PowerManager::getSupplyVoltageMilli(void)
{
    int adcRaw;
    uint32_t millis = 0;
    float result = NAN;
    int sampleCnt;

    for(sampleCnt = 7; sampleCnt >= 0; sampleCnt--) {
        adcRaw = adc1_get_raw(battVoltageChannel);
        if(adcRaw > -1) {
            millis += esp_adc_cal_raw_to_voltage(adcRaw, &battVoltageAdcCharacteristics);
        } else {
            break;
        }
        if(sampleCnt > 0) vTaskDelay(pdMS_TO_TICKS(10));
    }

    if(adcRaw > -1) {
        millis = millis>>3;
        ESP_LOGD(LOG_TAG_POWER_MANAGER, "batt voltage filtered, calibrated from ADC: %04d mV", millis);
        result = millis * battVoltageMult;
    } else {
        ESP_LOGE(LOG_TAG_POWER_MANAGER, "Error occurred during ADC conversion (batt voltage).");
    }

    return result;
}

void PowerManager::setPeripheralEnable(bool en)
{
    // TBD: mutex locking
    gpio_set_level(peripheralEnGpioNum, (en == true) ? 1 : 0);
    peripheralEnState = en;
}

bool PowerManager::getPeripheralEnable(void)
{
    return peripheralEnState;
}

void PowerManager::setPeripheralExtSupply(bool en)
{
    if((peripheralEnState == false) && (en == true)) {
        ESP_LOGW(LOG_TAG_POWER_MANAGER, "Global peripheral enable is not set, but external peripheral supply enable is requested.");
    }

    // TBD: mutex locking
    gpio_set_level(peripheralExtSupplyGpioNum, (en == true) ? 1 : 0);
    peripheralExtSupplyState = en;
}

bool PowerManager::getPeripheralExtSupply(void)
{
    return peripheralExtSupplyState;
}

bool PowerManager::getKeepAwake(void)
{
    return (gpio_get_level(keepAwakeGpioNum) == 0) ? true : false;
}

bool PowerManager::gotoSleep(void)
{
    bool ret = false;
    esp_err_t err;

    if(getKeepAwake() == true) {
        ESP_LOGI(LOG_TAG_POWER_MANAGER, "gotoSleep requested, but keep awake is set. Not going to sleep.");
    } else {
        // prepare for sleep
        esp_wifi_stop(); // ignore return value, because we don't care if WiFi was up before

        err = esp_sleep_enable_timer_wakeup(deepSleepTimeUs);
        if(ESP_OK != err) ESP_LOGE(LOG_TAG_POWER_MANAGER, "Error setting up deep sleep timer.");

        if(ESP_OK == err) {
            for(int domCnt=0; domCnt < ESP_PD_DOMAIN_MAX; domCnt++) {
                err = esp_sleep_pd_config((esp_sleep_pd_domain_t) domCnt, ESP_PD_OPTION_AUTO);
                if(ESP_OK != err) {
                    ESP_LOGE(LOG_TAG_POWER_MANAGER, "Error setting power domain %d to AUTO.", domCnt);
                    break;
                }
            }
        }

        // actually go to sleep
        if(ESP_OK == err) {
            esp_deep_sleep_start();
            ret = true; // previous function doesn't return, but this function must return something
        }
    }

    return ret;
}
