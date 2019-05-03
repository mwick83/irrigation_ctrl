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
    typedef enum err_t {
        ERR_OK = 0,
        ERR_INVALID_PARAM = -1,
        ERR_INVALID_HANDLE = -2,
        ERR_PARTIAL_EVENT_HANDLES = -3,
        ERR_NO_HANDLES_FOUND = -4,
        ERR_NO_STOP_SLOT_AVAIL = -5,
    } err_t;

    typedef struct event_handle_t {
        int  idx;
        bool isStart;
    } event_handle_t;

    IrrigationPlanner(void);
    ~IrrigationPlanner(void);

    time_t getNextEventTime(time_t startTime, bool excludeStartTime);
    err_t getEventHandles(time_t eventTime, event_handle_t* dest, unsigned int maxElements);
    err_t getEventData(event_handle_t handle, IrrigationEvent::irrigation_event_data_t* dest);
    err_t confirmEvent(event_handle_t handle);

private:
    const char* logTag = "irrig_planner";

    static const unsigned int numZones = 8;                                         /**< Number of configurable irrigation zones. */
    static const unsigned int numNormalEvents = 4*numZones;                         /**< Number of regular irrigation events. */
    static const unsigned int numSingleShotEvents = 1;                              /**< Number of temporary single shot irrigation events. */
    static const unsigned int numEvents = numNormalEvents + numSingleShotEvents;    /**< Number of irrigation events. */
    static const unsigned int numStopEvents = numZones+numSingleShotEvents;         /**< Number of irrigation stop events. */ // TBD: raise to numEvents + 1 ?

    irrigation_zone_cfg_t zones[numZones];      /**< Storage holding irrigation zone configurations. */

    IrrigationEvent events[numEvents];          /**< Storage holding irrigation events. */
    bool eventsUsed[numEvents];                 /**< Flag weather or not the corresponding event storage is used. */

    IrrigationEvent stopEvents[numStopEvents];  /**< Storage holding irrigation stop events. */
    bool stopEventsUsed[numStopEvents];         /**< Flag weather or not the corresponding stop event storage is used. */

    int getNextEventIdx(time_t startTime, IrrigationEvent* eventList, bool* eventUsedList, unsigned int listElements);
    void printEventDetails(IrrigationEvent* evt);

    bool confirmNormalEvent(unsigned int idx);
    void confirmStopEvent(unsigned int idx);
};

#endif /* IRRIGATION_PLANNER_H */
