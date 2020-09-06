#ifndef IRRIGATION_PLANNER_H
#define IRRIGATION_PLANNER_H

#include <stdint.h>
#include <ctime>
#include <vector>

#include "esp_log.h"

#include "irrigationEvent.h"
#include "hardwareConfig.h"

/** Number of configurable irrigation zones. */
constexpr unsigned int irrigationPlannerNumZones = 8;
/** Number of regular irrigation events. */
constexpr unsigned int irrigationPlannerNumNormalEvents = 4*irrigationPlannerNumZones;
/** Number of temporary single shot irrigation events. */
constexpr unsigned int irrigationPlannerNumSingleShotEvents = 1;

/** Number of irrigation events. */
static constexpr unsigned int irrigationPlannerNumEvents = irrigationPlannerNumNormalEvents + irrigationPlannerNumSingleShotEvents;
/** Number of irrigation stop events. */ // TBD: raise to numEvents + 1 ?
static constexpr unsigned int irrigationPlannerNumStopEvents = irrigationPlannerNumZones + irrigationPlannerNumSingleShotEvents;

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
        ERR_INVALID_ZONE_IDX = -6,
    } err_t;

    typedef struct event_handle_t {
        int  idx;
        bool isStart;
    } event_handle_t;

    typedef void(*IrrigConfigUpdateHookFncPtr)(void*);

    IrrigationPlanner();
    ~IrrigationPlanner();

    time_t getNextEventTime(time_t startTime, bool excludeStartTime);
    err_t getEventHandles(time_t eventTime, event_handle_t* dest, unsigned int maxElements);
    err_t getEventData(event_handle_t handle, IrrigationEvent::irrigation_event_data_t* dest);
    err_t confirmEvent(event_handle_t handle);

    err_t getZoneConfigPtr(int idx, irrigation_zone_cfg_t** cfg);

    void configurationUpdated();
    void registerConfigurationUpdatedHook(IrrigConfigUpdateHookFncPtr hook, void* param);

private:
    const char* logTag = "irrig_planner";

    irrigation_zone_cfg_t zones[irrigationPlannerNumZones];         /**< Storage holding irrigation zone configurations. */

    IrrigationEvent events[irrigationPlannerNumEvents];             /**< Storage holding irrigation events. */
    bool eventsUsed[irrigationPlannerNumEvents];                    /**< Flag weather or not the corresponding event storage is used. */

    IrrigationEvent stopEvents[irrigationPlannerNumStopEvents];     /**< Storage holding irrigation stop events. */
    bool stopEventsUsed[irrigationPlannerNumStopEvents];            /**< Flag weather or not the corresponding stop event storage is used. */

    bool configLock;                                                /**< Flag weather or not the config should be locked from updates. */
    bool configUpdatedDuringLock;                                   /**< Flag weather or not a config update happend during a locked phase. */

    IrrigConfigUpdateHookFncPtr configUpdatedHook;                  /**< Configuration updated hook function storage. */
    void* configUpdatedHookParamPtr;                                /**< Parameter storage for configuration updated hook function. */

    int getNextEventIdx(time_t startTime, IrrigationEvent* eventList, bool* eventUsedList, unsigned int listElements);
    void printEventDetails(IrrigationEvent* evt);
    void printAllEvents();

    bool confirmNormalEvent(unsigned int idx);
    void confirmStopEvent(unsigned int idx);
};

#endif /* IRRIGATION_PLANNER_H */
