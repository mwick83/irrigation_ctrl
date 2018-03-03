#include "outputController.h"

/**
 * @brief Default constructor, which performs basic initialization.
 */
OutputController::OutputController(void)
{
    // setup mapped GPIOs to inactive state
    for(int i = 0; i < (sizeof(intChannelMap) / sizeof(intChannelMap[0])); i++) {
        gpio_set_level(intChannelMap[i], 0);
        gpio_set_direction(intChannelMap[i], GPIO_MODE_OUTPUT);
    }
}

/**
 * @brief Default destructor, which cleans up allocated data.
 */
OutputController::~OutputController(void)
{
    // disable mapped GPIOs
    for(int i = 0; i < (sizeof(intChannelMap) / sizeof(intChannelMap[0])); i++) {
        gpio_set_level(intChannelMap[i], 0);
    }
}

/**
 * @brief Return wether or not any output is active.
 */
bool OutputController::anyOutputsActive(void)
{
    return (activeIntChannelMap != 0U);
}

/**
 * @brief Set an output channel to the desired value.
 * 
 * @param outputNum The output channel to be switched
 * @param switchOn Wether or not to switch the output on
 * @return OutputController::err_t
 * @retval ERR_OK Success.
 * @retval ERR_INVALID_PARAM outputNum has an invalid value.
 */
OutputController::err_t OutputController::setOutput(ch_map_t outputNum, bool switchOn)
{
    err_t ret = ERR_OK;

    if(outputNum >= NUM_CHANNELS) return ERR_INVALID_PARAM;

    if(outputNum < (sizeof(intChannelMap) / sizeof(intChannelMap[0]))) {
        ESP_LOGD(logTag, "Switching output %d (%s; GPIO %d) %s", outputNum, CH_MAP_TO_STR(outputNum),
                    intChannelMap[outputNum], switchOn ? "ON" : "OFF");
        gpio_set_level(intChannelMap[outputNum], switchOn ? 1U : 0U);

        uint32_t mapMask = 1U << outputNum;
        if(switchOn) {
            activeIntChannelMap |= mapMask;
        } else {
            activeIntChannelMap &= ~mapMask;
        }
    } else if(outputNum <= CH_EXT0) {
        ESP_LOGW(logTag, "External outputs not yet supported.");
        ret = ERR_INVALID_PARAM;
    } else {
        ret = ERR_INVALID_PARAM;
    }

    return ret;
}

/**
 * @brief Disable all outputs at once.
 * 
 */
void OutputController::disableAllOutputs(void)
{
    for(int outputNum = 0; outputNum < (sizeof(intChannelMap) / sizeof(intChannelMap[0])); outputNum++) {
        setOutput((ch_map_t) outputNum, false);
    }
}
