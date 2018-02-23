#ifndef IRRIGATION_PLANNER_H
#define IRRIGATION_PLANNER_H

#include <stdint.h>
#include <ctime>
#include <vector>

#include "esp_log.h"

#include "irrigationEvent.h"
#include "hardwareConfig.h"

/**
 * @brief The IrrigationPlanner class is a manager of IrrigationEvents. It is used
 * by the IrrigationController to determine what to do and when to do it.
 */
class IrrigationPlanner
{
public:
    typedef enum {
        ERR_OK = 0,
        ERR_INVALID_PARAM = -1,
        ERR_NO_EVENT_DATA_FOUND = -2,
        ERR_PARTIAL_EVENT_DATA = -3,
    } err_t;

    IrrigationPlanner(void);
    ~IrrigationPlanner(void);

    time_t getNextEventTime(void);
    time_t getNextEventTime(time_t startTime, bool excludeStartTime);
    err_t getEventChannelConfig(time_t eventTime, std::vector<IrrigationEvent::ch_cfg_t>* dest);

private:
    const char* logTag = "irrig_planner";

    std::vector<IrrigationEvent*> events; /**< Vector to store all irrigation events */
};

#endif /* IRRIGATION_PLANNER_H */
