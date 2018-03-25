#include "irrigationPlanner.h"

#include "outputController.h" //temporarily needed for CH_MAIN, ...

//#define IRRIGATION_PLANNER_PRINT_ALL_EVENTS
//#define IRRIGATION_PLANNER_NEXT_EVENT_DEBUG

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
    const int hours[] = {7, 12, 20};
    for(int i = 0; i < (sizeof(hours)/sizeof(hours[0])); i++) {
        event = new IrrigationEvent();
        event->setDailyRepetition(hours[i], 0, 0);
        event->addChannelConfig(OutputController::CH_MAIN, true);
        events.push_back(event);
        event = new IrrigationEvent();
        event->setDailyRepetition(hours[i], 0, 15);
        event->addChannelConfig(OutputController::CH_AUX1, true);
        events.push_back(event);
        event = new IrrigationEvent();
        event->setDailyRepetition(hours[i], 0, 20);
        event->addChannelConfig(OutputController::CH_AUX0, true);
        events.push_back(event);
        event = new IrrigationEvent();
        event->setDailyRepetition(hours[i], 1, 0);
        event->addChannelConfig(OutputController::CH_MAIN, false);
        events.push_back(event);
        event = new IrrigationEvent();
        event->setDailyRepetition(hours[i], 1, 20);
        event->addChannelConfig(OutputController::CH_AUX0, false);
        events.push_back(event);
        event = new IrrigationEvent();
        event->setDailyRepetition(hours[i], 1, 30);
        event->addChannelConfig(OutputController::CH_AUX1, false);
        events.push_back(event);
    }

    #ifdef IRRIGATION_PLANNER_PRINT_ALL_EVENTS
        // print all events out for debugging
        for(std::vector<IrrigationEvent*>::iterator it = events.begin() ; it != events.end(); ++it) {
            (*it)->updateReferenceTime(time(NULL));
            time_t eventTime = (*it)->getNextOccurance();
            struct tm eventTm;
            localtime_r(&eventTime, &eventTm);

            for(int i = 0; i < (*it)->getChannelConfigSize(); i++) {
                uint32_t chNum;
                bool switchOn;
                (*it)->getChannelConfigInfo(i, &chNum, &switchOn);
                ESP_LOGD(logTag, "Event at %02d.%02d.%04d %02d:%02d:%02d, channel = %d, switchOn = %d", 
                    eventTm.tm_mday, eventTm.tm_mon+1, 1900+eventTm.tm_year,
                    eventTm.tm_hour, eventTm.tm_min, eventTm.tm_sec,
                    chNum, switchOn ? 1:0);
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
    #ifdef IRRIGATION_PLANNER_NEXT_EVENT_DEBUG
        std::vector<IrrigationEvent::ch_cfg_t> chCfg;
    #endif

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
            chCfg.clear();
            (*it)->appendChannelConfig(&chCfg);
            for(std::vector<IrrigationEvent::ch_cfg_t>::iterator chIt = chCfg.begin(); chIt != chCfg.end(); chIt++) {
                ESP_LOGD(logTag, "Event at %02d.%02d.%04d %02d:%02d:%02d, channel = %d, switchOn = %d", 
                    eventTm.tm_mday, eventTm.tm_mon+1, 1900+eventTm.tm_year,
                    eventTm.tm_hour, eventTm.tm_min, eventTm.tm_sec,
                    (*chIt).chNum, (*chIt).switchOn ? 1:0);
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
IrrigationPlanner::err_t IrrigationPlanner::getEventChannelConfig(time_t eventTime,
    std::vector<IrrigationEvent::ch_cfg_t>* dest)
{
    err_t ret = ERR_OK;
    if(nullptr == dest) return ERR_INVALID_PARAM;

    // clear destination vector
    dest->clear();

    for(std::vector<IrrigationEvent*>::iterator it = events.begin() ; it != events.end(); ++it) {
        (*it)->updateReferenceTime(eventTime);
        time_t curEventTime = (*it)->getNextOccurance();

        if(curEventTime == eventTime) {
            if(IrrigationEvent::ERR_OK != (*it)->appendChannelConfig(dest)) {
                ret = ERR_PARTIAL_EVENT_DATA;
            }
        }
    }

    if((ERR_OK == ret) && (dest->size() == 0)) ret = ERR_NO_EVENT_DATA_FOUND;

    return ret;
}
