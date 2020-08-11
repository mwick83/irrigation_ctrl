#ifndef IRRIGATION_ZONE_CFG_H
#define IRRIGATION_ZONE_CFG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "outputController.h"

constexpr unsigned int irrigationZoneCfgElements = 4; // TBD: use (OutputController::intChannels + OutputController::extChannels)?
constexpr unsigned int irrigationZoneCfgNameLen = 15;

typedef struct irrigation_zone_cfg_t_ {
    char                        name[irrigationZoneCfgNameLen+1];
    bool                        chEnabled[irrigationZoneCfgElements];
    OutputController::ch_map_t  chNum[irrigationZoneCfgElements];
    bool                        chStateStart[irrigationZoneCfgElements];
    bool                        chStateStop[irrigationZoneCfgElements];
} irrigation_zone_cfg_t;

#ifdef __cplusplus
};
#endif

#endif /* IRRIGATION_ZONE_CFG_H */
