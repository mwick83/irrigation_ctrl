#ifndef OUTPUT_CONTROLLER_H
#define OUTPUT_CONTROLLER_H

#include <stdint.h>

#include "esp_log.h"

#include "hardwareConfig.h"

#define CH_MAP_TO_STR(num) (\
    (num == OutputController::CH_MAIN) ? "MAIN" : \
    (num == OutputController::CH_AUX0) ? "AUX0" : \
    (num == OutputController::CH_AUX1) ? "AUX1" : \
    (num == OutputController::CH_EXT0) ? "EXT0" : \
    "UNKOWN" \
)

/**
 * @brief The OutputController class is the abstraction layer for mapping
 * channels to actual hardware outputs.
 */
class OutputController
{
public:
    typedef enum {
        CH_MAIN = 0,
        CH_AUX0 = 1,
        CH_AUX1 = 2,
        CH_EXT0 = 32,
        NUM_CHANNELS
    } ch_map_t;

    typedef enum {
        ERR_OK = 0,
        ERR_INVALID_PARAM = -1,
    } err_t;

    OutputController(void);
    ~OutputController(void);

    bool anyOutputsActive(void);
    err_t setOutput(ch_map_t outputNum, bool switchOn);
    void disableAllOutputs(void);

private:
    const char* logTag = "out_ctrl";

    /** Lookup table to map channel numbers to GPIO number. Doesn't include external channels. */
    const gpio_num_t intChannelMap[CH_AUX1+1] = {
        irrigationMainGpioNum,
        irrigationAux0GpioNum,
        irrigationAux1GpioNum
    };

    uint32_t activeIntChannelMap;
};

#endif /* OUTPUT_CONTROLLER_H */
