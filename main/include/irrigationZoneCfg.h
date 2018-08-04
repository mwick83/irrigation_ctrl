#ifndef IRRIGATION_ZONE_CFG_H
#define IRRIGATION_ZONE_CFG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "outputController.h"

const unsigned int irrigationZoneConfigElements = 4;

typedef struct irrigation_zone_cfg_t_ {
    char* const                 name;
    bool                        chEnabled[irrigationZoneConfigElements];
    OutputController::ch_map_t  chNum[irrigationZoneConfigElements];
    bool                        chStateStart[irrigationZoneConfigElements];
    bool                        chStateStop[irrigationZoneConfigElements];
} irrigation_zone_cfg_t;

#ifdef __cplusplus
};
#endif

#endif /* IRRIGATION_ZONE_CFG_H */
