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
