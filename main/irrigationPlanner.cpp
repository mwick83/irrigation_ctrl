#include "irrigationPlanner.h"

#include "outputController.h" // needed for CH_MAIN, ...
#include "irrigationZoneCfg.h"

//#define IRRIGATION_PLANNER_PRINT_ALL_EVENTS
//#define IRRIGATION_PLANNER_NEXT_EVENT_DEBUG
//#define IRRIGATION_PLANNER_STOP_EVENT_DEBUG

/**
 * @brief Default constructor, which performs basic initialization.
 */
IrrigationPlanner::IrrigationPlanner(void)
{
    // Prepare event and zone storages
    for(int i = 0; i < numZones; i++) {
        zones[i].name[irrigationZoneCfgNameLen] = '\0';
        for(int j=0; j < irrigationZoneCfgElements; j++) {
            zones[i].chEnabled[j] = false;
        }
    }
    for(int i = 0; i < numEvents; i++) {
        eventsUsed[i] = false;
    }
    for(int i = 0; i < numStopEvents; i++) {
        stopEventsUsed[i] = false;
    }

    // Setup fixed zones
    strncpy(zones[0].name, "MAIN", irrigationZoneCfgNameLen);
    zones[0].chEnabled[0] = true;
    zones[0].chNum[0] = OutputController::CH_MAIN;
    zones[0].chStateStart[0] = true;
    zones[0].chStateStop[0] = false;

    strncpy(zones[1].name, "AUX0", irrigationZoneCfgNameLen);
    zones[1].chEnabled[0] = true;
    zones[1].chNum[0] = OutputController::CH_AUX0;
    zones[1].chStateStart[0] = true;
    zones[1].chStateStop[0] = false;

    strncpy(zones[2].name, "AUX1", irrigationZoneCfgNameLen);
    zones[2].chEnabled[0] = true;
    zones[2].chNum[0] = OutputController::CH_AUX1;
    zones[2].chStateStart[0] = true;
    zones[2].chStateStop[0] = false;

    // Setup a fixed irrigation plan for now:
    // 3 times a day enable the pump (on channel 'main').
    const int hours[] = {8, 12, 21};
    const int numHours = sizeof(hours)/sizeof(hours[0]);
    for(int i = 0; i < numHours; i++) {
        events[i*numHours].setDailyRepetition(hours[i], 0, 0);
        events[i*numHours].setStartFlag(true);
        events[i*numHours].setDuration(60);
        events[i*numHours].setZoneConfig(&zones[0]);
        eventsUsed[i*numHours] = true;

        events[i*numHours+1].setDailyRepetition(hours[i], 0, 15);
        events[i*numHours+1].setStartFlag(true);
        events[i*numHours+1].setDuration(45);
        events[i*numHours+1].setZoneConfig(&zones[2]);
        eventsUsed[i*numHours+1] = true;

        events[i*numHours+2].setDailyRepetition(hours[i], 0, 20);
        events[i*numHours+2].setStartFlag(true);
        events[i*numHours+2].setDuration(30);
        events[i*numHours+2].setZoneConfig(&zones[1]);
        eventsUsed[i*numHours+2] = true;
    }

    #ifdef IRRIGATION_PLANNER_PRINT_ALL_EVENTS
        // print all events out for debugging
        for(int i = 0; i < numEvents; i++) {
            if(eventsUsed[i]) {
                IrrigationEvent* evt = &events[i];
                evt->updateReferenceTime(time(NULL));
                time_t eventTime = evt->getNextOccurance();
                struct tm eventTm;
                localtime_r(&eventTime, &eventTm);

                printEventDetails(evt);
            }
        }
    #endif
}

/**
 * @brief Default destructor, which cleans up allocated data.
 */
IrrigationPlanner::~IrrigationPlanner(void)
{
    // clean up all events and zones
    for(int i = 0; i < numZones; i++) {
        zones[i].name[irrigationZoneCfgNameLen] = '\0';
        for(int j=0; j < irrigationZoneCfgElements; j++) {
            zones[i].chEnabled[j] = false;
        }
    }
    for(int i = 0; i < (numZones * 4); i++) {
        eventsUsed[i] = false;
    }
    for(int i = 0; i < (numZones + 1); i++) {
        stopEventsUsed[i] = false;
    }
}

/**
 * @brief Get the time of the next occuring event starting at startTime.
 * 
 * @param startTime Start time to consider for searching the next occurance
 * @param excludeStartTime If true, only search for events later than startTime, not equal.
 * @return time_t Time of the next occuring event.
 */
time_t IrrigationPlanner::getNextEventTime(time_t startTime, bool excludeStartTime)
{
    time_t nextEventTime = 0;

    int nextStartEventIdx;
    int nextStopEventIdx;

    if(excludeStartTime) {
        // convert startTime to time to easily increase the startTime by one sec
        struct tm startTimeTm;
        localtime_r(&startTime, &startTimeTm);
        startTimeTm.tm_sec++;
        // and convert it back to time_t, inc. wrap-arounds
        startTime = mktime(&startTimeTm);
    }

    // getting the index will update all of the reference times as well
    nextStartEventIdx = getNextEventIdx(startTime, events, eventsUsed, numEvents);
    nextStopEventIdx = getNextEventIdx(startTime, stopEvents, stopEventsUsed, numStopEvents);

    if(nextStartEventIdx >= 0) {
        if(nextStopEventIdx >= 0) {
            if(events[nextStartEventIdx] < stopEvents[nextStopEventIdx]) {
                nextEventTime = events[nextStartEventIdx].getNextOccurance();
            } else {
                nextEventTime = stopEvents[nextStopEventIdx].getNextOccurance();
            }
        } else {
            nextEventTime = events[nextStartEventIdx].getNextOccurance();
        }
    } else {
        if(nextStopEventIdx >= 0) {
            nextEventTime = stopEvents[nextStopEventIdx].getNextOccurance();
        }
    }

    return nextEventTime;
}

/**
 * @brief Get the index of the next upcoming event in the specified list.
 * 
 * @param startTime Start time to consider for searching the next occurance
 * @param eventList Event list to search.
 * @param eventUsedList Used event list corresponding to eventList.
 * @return int Inedx the next occuring event within the list.
 */
int IrrigationPlanner::getNextEventIdx(time_t startTime, IrrigationEvent* eventList, bool* eventUsedList, 
    unsigned int listElements)
{
    int nextIdx = -1;
    IrrigationEvent* nextEvent = nullptr;

    for(int i=0; i < listElements; i++) {
        if((nullptr != nextEvent) && eventUsedList[i]) {
            eventList[i].updateReferenceTime(startTime);
            if((eventList[i].getNextOccurance() != 0) && (eventList[i] < *nextEvent)) {
                nextIdx = i;
                nextEvent = &eventList[i];

                #ifdef IRRIGATION_PLANNER_NEXT_EVENT_DEBUG
                    printEventDetails(nextEvent);
                    ESP_LOGD(logTag, "This is our new candidate!");
                #endif
            }
        } else if(eventUsedList[i]) {
            eventList[i].updateReferenceTime(startTime);
            if(eventList[i].getNextOccurance() != 0) {
                nextIdx = i;
                nextEvent = &eventList[i];

                #ifdef IRRIGATION_PLANNER_NEXT_EVENT_DEBUG
                    printEventDetails(nextEvent);
                    ESP_LOGD(logTag, "This is our new candidate!");
                #endif
            }
        }
    }

    return nextIdx;
}

/**
 * @brief Get all event handles corresponding to the specified time.
 * 
 * Note: The memory area pointed to by dest will be cleared entirely.
 * 
 * @param eventTime Time to get the event handles for.
 * @param dest Pointer to a memory area, which will be populated with the event handles.
 * @param maxElements Number of elements the memory area can hold.
 * @return IrrigationPlanner::err_t
 * @retval ERR_OK Success.
 * @retval ERR_PARTIAL_EVENT_HANDLES Not enough space available at dest.
 * @retval ERR_NO_HANDLES_FOUND No handles found for the specified time.
 */
IrrigationPlanner::err_t IrrigationPlanner::getEventHandles(time_t eventTime,
    IrrigationPlanner::event_handle_t* dest, unsigned int maxElements)
{
    err_t ret = IrrigationPlanner::ERR_OK;

    unsigned int handleCnt = 0;
    time_t checkEventTime = 0;

    // check start event list
    for(int i=0; i < numEvents; i++) {
        if(eventsUsed[i]) {
            checkEventTime = events[i].getNextOccurance();
            if(checkEventTime == eventTime) {
                if(handleCnt < maxElements) {
                    dest[handleCnt].idx = i;
                    dest[handleCnt].isStart = true;
                    handleCnt++;
                } else {
                    ret = ERR_PARTIAL_EVENT_HANDLES;
                    break;
                }
            }
        }
    }

    // check stop event list
    for(int i=0; i < numStopEvents; i++) {
        if(stopEventsUsed[i]) {
            checkEventTime = stopEvents[i].getNextOccurance();
            if(checkEventTime == eventTime) {
                if(handleCnt < maxElements) {
                    dest[handleCnt].idx = i;
                    dest[handleCnt].isStart = false;
                    handleCnt++;
                } else {
                    ret = ERR_PARTIAL_EVENT_HANDLES;
                    break;
                }
            }
        }
    }

    // clear remaining list elements
    for(int i=handleCnt; i<maxElements; i++) {
        dest[i].idx = -1;
    }

    // check if we actually found something
    if(handleCnt == 0) {
        ret = ERR_NO_HANDLES_FOUND;
    }

    return ret;
}

/**
 * @brief Get channel configuration for the specified event.
 * 
 * Note: The data area pointed to by dest will be cleared.
 * 
 * @param handle Handle to get the configuration for.
 * @param dest Pointer to a memory area, which will be populated with the channel configuration.
 * @return IrrigationPlanner::err_t
 * @retval ERR_OK Success.
 * @retval ERR_INVALID_PARAM dest is invalid.
 * @retval ERR_INVALID_HANDLE The specified event handle is invalid.
 */
IrrigationPlanner::err_t IrrigationPlanner::getEventData(IrrigationPlanner::event_handle_t handle,
    IrrigationEvent::irrigation_event_data_t* dest)
{
    err_t ret = ERR_OK;

    if(nullptr == dest) return ERR_INVALID_PARAM;
    if(handle.idx < 0) return ERR_INVALID_HANDLE;

    if(handle.isStart) {
        if(handle.idx >= numEvents) {
            ret = ERR_INVALID_HANDLE;
        } else if(!eventsUsed[handle.idx]) {
            ret = ERR_INVALID_HANDLE;
        } else {
            //memcpy((void*) dest, (void*) &events[handle.idx], sizeof(IrrigationEvent::irrigation_event_data_t));
            events[handle.idx].getEventData(dest);
        }
    } else {
        if(handle.idx >= numStopEvents) {
            ret = ERR_INVALID_HANDLE;
        } else if(!stopEventsUsed[handle.idx]) {
            ret = ERR_INVALID_HANDLE;
        } else {
            stopEvents[handle.idx].getEventData(dest);
        }
    }

    return ret;
}

/**
 * @brief Confirm the specified event to proceed it in the schedule
 * 
 * @param handle Handle to the event to be confirmed.
 * @return IrrigationPlanner::err_t
 * @retval ERR_OK Success.
 * @retval ERR_INVALID_HANDLE The specified event handle is invalid.
 * @retval ERR_NO_STOP_SLOT_AVAIL No stop event slot available.
 */
IrrigationPlanner::err_t IrrigationPlanner::confirmEvent(IrrigationPlanner::event_handle_t handle)
{
    err_t ret = ERR_OK;

    if(handle.idx < 0) return ERR_INVALID_HANDLE;

    if(handle.isStart) {
        if(handle.idx >= numEvents) {
            ret = ERR_INVALID_HANDLE;
        } else if(!eventsUsed[handle.idx]) {
            ret = ERR_INVALID_HANDLE;
        } else {
            if(!confirmNormalEvent(handle.idx)) {
                ret = ERR_NO_STOP_SLOT_AVAIL;
            }
        }
    } else {
        if(handle.idx >= numStopEvents) {
            ret = ERR_INVALID_HANDLE;
        } else if(!stopEventsUsed[handle.idx]) {
            ret = ERR_INVALID_HANDLE;
        } else {
            confirmStopEvent(handle.idx);
        }
    }

    return ret;
}

/**
 * @brief Confirm a normal event to proceed it in the schedule
 * 
 * @param idx Index of the event to be confirmed.
 * @return bool
 * @retval true Success.
 * @retval false Enqueuing a stop event failed due to no available stop slots.
 */
bool IrrigationPlanner::confirmNormalEvent(unsigned int idx)
{
    bool ret = false;
    IrrigationEvent::irrigation_event_data_t evtData;

    for(int i=0; i<numStopEvents; i++) {
        if(!stopEventsUsed[i]) {
            // Mark the slot as used
            stopEventsUsed[i] = true;

            // get event data
            IrrigationEvent::err_t eventErr;
            eventErr = events[idx].getEventData(&evtData);
            if(IrrigationEvent::ERR_OK != eventErr) {
                ESP_LOGE(logTag, "Error getting event data: %d. Cannot add stop event!", eventErr);
            } else {
                // Calculate the actual time
                time_t stopTime = events[idx].getNextOccurance();

                // a) Convert reference time_t to struct tm for recalculation
                struct tm stopTimeTm;
                localtime_r(&stopTime, &stopTimeTm);
                stopTimeTm.tm_sec += evtData.durationSecs;
                //    set DST status to not available, because it may be different by
                //    the modifications modifications
                stopTimeTm.tm_isdst = -1;

                #ifdef IRRIGATION_PLANNER_STOP_EVENT_DEBUG
                    ESP_LOGD(logTag, "stopTime: %lu", stopTime);
                    ESP_LOGD(logTag, "stopTimeTm: %d.%d.%d %d:%d:%d",
                        stopTimeTm.tm_mday, stopTimeTm.tm_mon, stopTimeTm.tm_year,
                        stopTimeTm.tm_hour, stopTimeTm.tm_min, stopTimeTm.tm_sec);
                #endif

                // b) Use mktime to fixup the time and convert time_t back to struct tm
                stopTime = mktime(&stopTimeTm);
                localtime_r(&stopTime, &stopTimeTm);
                #ifdef IRRIGATION_PLANNER_STOP_EVENT_DEBUG
                    ESP_LOGD(logTag, "after recalc: stopTime: %lu", stopTime);
                    ESP_LOGD(logTag, "after recalc: stopTimeTm: %d.%d.%d %d:%d:%d",
                        stopTimeTm.tm_mday, stopTimeTm.tm_mon, stopTimeTm.tm_year,
                        stopTimeTm.tm_hour, stopTimeTm.tm_min, stopTimeTm.tm_sec);
                #endif

                // And now set properties of the stop event
                stopEvents[i].setSingleEvent(stopTimeTm.tm_hour, stopTimeTm.tm_min, stopTimeTm.tm_sec,
                    stopTimeTm.tm_mday, stopTimeTm.tm_mon + 1, stopTimeTm.tm_year + 1900);
                stopEvents[i].setStartFlag(false);
                stopEvents[i].setDuration(0);
                stopEvents[i].setZoneConfig(evtData.zoneConfig);
                stopEvents[i].updateReferenceTime(events[idx].getReferenceTime());

                #ifdef IRRIGATION_PLANNER_STOP_EVENT_DEBUG
                    printEventDetails(&stopEvents[i]);
                #endif

                ret = true;
            }

            break;
        }
    }

    // Check for single shot event to disable it
    // Note: Do this independant if we managed to set a stop event above;
    // cleaning up is more important to stay operational
    if(idx >= numNormalEvents) {
        eventsUsed[idx] = false;
    }

    return ret;
}

/**
 * @brief Confirm a stop event to clear the slot and make it available again.
 * 
 * @param idx Index of the stop event to be confirmed.
 */
void IrrigationPlanner::confirmStopEvent(unsigned int idx)
{
    stopEventsUsed[idx] = false;
}

/**
 * @brief Print details of an event via debug log.
 * 
 * @param evt Pointer to the event to print.
 */
void IrrigationPlanner::printEventDetails(IrrigationEvent* evt)
{
    struct tm eventTm;
    time_t eventTime = evt->getNextOccurance();
    localtime_r(&eventTime, &eventTm);

    IrrigationEvent::irrigation_event_data_t curEventData;
    if(IrrigationEvent::ERR_OK != evt->getEventData(&curEventData)) {
        ESP_LOGE(logTag, "Error retrieving event data.");
    } else {
        if(nullptr != curEventData.zoneConfig) {
            bool isStartEvent = curEventData.isStart;
            ESP_LOGD(logTag, "Event at %02d.%02d.%04d %02d:%02d:%02d, zone = %s, duration = %d s, start: %s",
                eventTm.tm_mday, eventTm.tm_mon+1, 1900+eventTm.tm_year,
                eventTm.tm_hour, eventTm.tm_min, eventTm.tm_sec,
                curEventData.zoneConfig->name, curEventData.durationSecs, isStartEvent ? "yes" : "no");
            for(int i = 0; i < irrigationZoneCfgElements; i++) {
                if(curEventData.zoneConfig->chEnabled[i]) {
                    ESP_LOGD(logTag, "* Channel: %s, state: %s",
                        CH_MAP_TO_STR(curEventData.zoneConfig->chNum[i]),
                        isStartEvent ? (curEventData.zoneConfig->chStateStart[i] ? "ON" : "OFF") :
                                       (curEventData.zoneConfig->chStateStop[i] ? "ON" : "OFF"));
                }
            }
        } else {
            ESP_LOGW(logTag, "No valid zone config found for event at %02d.%02d.%04d %02d:%02d:%02d",
                eventTm.tm_mday, eventTm.tm_mon+1, 1900+eventTm.tm_year,
                eventTm.tm_hour, eventTm.tm_min, eventTm.tm_sec);
        }
    }
}
