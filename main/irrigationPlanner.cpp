#include "irrigationPlanner.h"

#include "outputController.h" // needed for CH_MAIN, ...
#include "irrigationZoneCfg.h"

#define IRRIGATION_PLANNER_PRINT_ALL_EVENTS
#define IRRIGATION_PLANNER_NEXT_EVENT_DEBUG

irrigation_zone_cfg_t irrigationPlannerZones[] = {
    {
        .name = "MAIN",
        .chEnabled = {true, false, false, false},
        .chNum = {OutputController::CH_MAIN, },
        .chStateStart = {true, },
        .chStateStop = {false, },
    },
    {
        .name = "AUX0",
        .chEnabled = {true, false, false, false},
        .chNum = {OutputController::CH_AUX0, },
        .chStateStart = {true, },
        .chStateStop = {false, },
    },
    {
        .name = "AUX1",
        .chEnabled = {true, false, false, false},
        .chNum = {OutputController::CH_AUX1, },
        .chStateStart = {true, },
        .chStateStop = {false, },
    },
};

/**
 * @brief Default constructor, which performs basic initialization.
 */
IrrigationPlanner::IrrigationPlanner(void)
{
    // pre-allocate space for irrigation events to save some re-allocation time when
    // adding elements below
    events.reserve(3*6);

    IrrigationEvent* event;

    // Setup a fixed irrigation plan for now:
    // 3 times a day enable the pump (on channel 'main') and turn it off
    // again after one minute.
    const int hours[] = {8, 12, 21};
    for(int i = 0; i < (sizeof(hours)/sizeof(hours[0])); i++) {
        event = new IrrigationEvent();
        event->setDailyRepetition(hours[i], 0, 0);
        event->setStartFlag(true);
        event->setDuration(180);
        event->setZoneConfig(&irrigationPlannerZones[0]);
        events.push_back(event);
#if 0
        event = new IrrigationEvent();
        event->setDailyRepetition(hours[i], 0, 15);
        event->setStartFlag(true);
        event->setDuration(60);
        event->setZoneConfig(&irrigationPlannerZones[2]);
        events.push_back(event);
        event = new IrrigationEvent();
        event->setDailyRepetition(hours[i], 0, 20);
        event->setStartFlag(true);
        event->setDuration(45);
        event->setZoneConfig(&irrigationPlannerZones[1]);
        events.push_back(event);
#endif
    }

    #ifdef IRRIGATION_PLANNER_PRINT_ALL_EVENTS
        // print all events out for debugging
        for(std::vector<IrrigationEvent*>::iterator it = events.begin() ; it != events.end(); ++it) {
            (*it)->updateReferenceTime(time(NULL));
            time_t eventTime = (*it)->getNextOccurance();
            struct tm eventTm;
            localtime_r(&eventTime, &eventTm);

            IrrigationEvent::irrigation_event_data_t curEventData;
            if(IrrigationEvent::ERR_OK != (*it)->getEventData(&curEventData)) {
                ESP_LOGE(logTag, "Error retrieving event data.");
            } else {
                if(nullptr != curEventData.zoneConfig) {
                    ESP_LOGD(logTag, "Event at %02d.%02d.%04d %02d:%02d:%02d, zone = %s, duration = %d ms",
                        eventTm.tm_mday, eventTm.tm_mon+1, 1900+eventTm.tm_year,
                        eventTm.tm_hour, eventTm.tm_min, eventTm.tm_sec,
                        curEventData.zoneConfig->name, curEventData.durationMillis);
                    for(int i = 0; i < irrigationZoneConfigElements; i++) {
                        if(curEventData.zoneConfig->chEnabled[i]) {
                        ESP_LOGD(logTag, "* channel = %d, switchOn = %d", 
                            curEventData.zoneConfig->chNum[i], curEventData.zoneConfig->chStateStart[i] ? 1:0);
                        }
                    }
                } else {
                    ESP_LOGW(logTag, "No valid zone config found for event at %02d.%02d.%04d %02d:%02d:%02d",
                        eventTm.tm_mday, eventTm.tm_mon+1, 1900+eventTm.tm_year,
                        eventTm.tm_hour, eventTm.tm_min, eventTm.tm_sec);
                }
            }
        }
    #endif
}

/**
 * @brief Default destructor, which cleans up allocated data.
 */
IrrigationPlanner::~IrrigationPlanner(void)
{
    // clean up all events
    for(std::vector<IrrigationEvent*>::iterator it = events.begin() ; it != events.end(); ++it) {
        delete *it;
    }
}

/**
 * @brief Get the time of the next occuring event starting from now.
 * 
 * @return time_t Time of the next event
 */
time_t IrrigationPlanner::getNextEventTime(void)
{
    time_t now = time(nullptr);
    return getNextEventTime(now, false);
}

/**
 * @brief Get the time of the next occuring event starting at startTime.
 * 
 * @param startTime Start time to consider for searching the next occurance
 * @param excludeStartTime If true, only search for events later than startTime, not equal.
 * @return time_t Time of the next event.
 */
time_t IrrigationPlanner::getNextEventTime(time_t startTime, bool excludeStartTime)
{
    time_t next = 0;

    if(excludeStartTime) {
        // convert startTime to time to easily increase the startTime by one sec
        struct tm startTimeTm;
        localtime_r(&startTime, &startTimeTm);
        startTimeTm.tm_sec++;
        // and convert it back to time_t, inc. wrap-arounds
        startTime = mktime(&startTimeTm);
    }

    for(std::vector<IrrigationEvent*>::iterator it = events.begin() ; it != events.end(); ++it) {
        (*it)->updateReferenceTime(startTime);
        time_t eventTime = (*it)->getNextOccurance();

        struct tm eventTm;
        localtime_r(&eventTime, &eventTm);

        #ifdef IRRIGATION_PLANNER_NEXT_EVENT_DEBUG
            IrrigationEvent::irrigation_event_data_t curEventData;
            if(IrrigationEvent::ERR_OK != (*it)->getEventData(&curEventData)) {
                ESP_LOGE(logTag, "Error retrieving event data.");
            } else {
                if(nullptr != curEventData.zoneConfig) {
                    ESP_LOGD(logTag, "Event at %02d.%02d.%04d %02d:%02d:%02d, zone = %s, duration = %d ms, start = %d",
                        eventTm.tm_mday, eventTm.tm_mon+1, 1900+eventTm.tm_year,
                        eventTm.tm_hour, eventTm.tm_min, eventTm.tm_sec,
                        curEventData.zoneConfig->name, curEventData.durationMillis, curEventData.isStart ? 1:0);
                    for(int i = 0; i < irrigationZoneConfigElements; i++) {
                        if(curEventData.zoneConfig->chEnabled[i]) {
                            ESP_LOGD(logTag, "* channel = %d, switchOn = %d",
                                curEventData.zoneConfig->chNum[i],
                                (curEventData.isStart ? curEventData.zoneConfig->chStateStart[i] : curEventData.zoneConfig->chStateStop[i]) ? 1:0);
                        }
                    }
                } else {
                    ESP_LOGW(logTag, "No valid zone config found for event at %02d.%02d.%04d %02d:%02d:%02d",
                        eventTm.tm_mday, eventTm.tm_mon+1, 1900+eventTm.tm_year,
                        eventTm.tm_hour, eventTm.tm_min, eventTm.tm_sec);
                }
            }
        #endif

        if((next == 0) || (next > eventTime)) {
            #ifdef IRRIGATION_PLANNER_NEXT_EVENT_DEBUG
                ESP_LOGD(logTag, "This is our new candidate!");
            #endif
            next = eventTime;
        }

    }

    return next;
}

/**
 * @brief Get all channel configurations for the specified time.
 * 
 * The returned channel configurations are the collection of all events
 * occurring at the specified time. There will be no destinction between 
 * multiple events.
 * 
 * Note: The vector pointed to by dest will be cleared.
 * 
 * @param eventTime Time to get the channel config infos for.
 * @param dest Pointer to a vector, which will be populated with the channel configuration.
 * @return IrrigationPlanner::err_t 
 * @retval ERR_OK Success.
 * @retval ERR_INVALID_PARAM dest is invalid.
 * @retval ERR_PARTIAL_EVENT_DATA An error occured while getting one of the channel configurations. Data may be incomplete.
 * @retval ERR_NO_EVENT_DATA_FOUND No event or no channel configurations could be found for the specified time.
 */
IrrigationPlanner::err_t IrrigationPlanner::getEventData(time_t eventTime, 
    std::vector<IrrigationEvent::irrigation_event_data_t>* dest)
{
    err_t ret = ERR_OK;
    IrrigationEvent::irrigation_event_data_t tmpData;

    if(nullptr == dest) return ERR_INVALID_PARAM;

    // clear destination vector
    dest->clear();

    // TBD: consider thread safety and on-the-fly changes to events!
    for(std::vector<IrrigationEvent*>::iterator it = events.begin() ; it != events.end(); ++it) {
        (*it)->updateReferenceTime(eventTime);
        time_t curEventTime = (*it)->getNextOccurance();

        if(curEventTime == eventTime) {
            if(IrrigationEvent::ERR_OK != (*it)->getEventData(&tmpData)) {
                ret = ERR_PARTIAL_EVENT_DATA;
            } else {
                dest->push_back(tmpData);
            }
        }
    }

    if((ERR_OK == ret) && (dest->size() == 0)) ret = ERR_NO_EVENT_DATA_FOUND;

    return ret;
}
