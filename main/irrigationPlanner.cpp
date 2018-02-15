#include "irrigationPlanner.h"

/**
 * @brief Default constructor, which performs basic initialization.
 */
IrrigationPlanner::IrrigationPlanner(void)
{
    IrrigationEvent* event;

    // Setup a fixed irrigation plan for now:
    // 3 times a day enable the pump (on channel 'main') and turn it off
    // again after one minute.
    const int hours[] = {7, 12, 20};
    for(int i = 0; i < (sizeof(hours)/sizeof(hours[0])); i++) {
        event = new IrrigationEvent();
        event->setDailyRepetition(hours[i], 0, 0);
        event->addChannelConfig(CH_MAIN, true);
        events.push_back(event);
        event = new IrrigationEvent();
        event->setDailyRepetition(hours[i], 1, 0);
        event->addChannelConfig(CH_MAIN, false);
        events.push_back(event);
    }

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
