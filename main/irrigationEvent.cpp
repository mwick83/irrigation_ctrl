#include "irrigationEvent.h"

/**
 * @brief Default constructor, which performs basic initialization.
 */
IrrigationEvent::IrrigationEvent(void)
{
    // make this event invalid
    repetitionType = NOT_SET;
    refTime = 0;

    eventData.zoneIdx = -1;
    eventData.durationSecs = 1;
    eventData.isStart = true;
    eventData.parentPtr = this;
}

/**
 * @brief Default destructor, which cleans up allocated data.
 */
IrrigationEvent::~IrrigationEvent(void)
{
}

IrrigationEvent::err_t IrrigationEvent::setZoneIndex(int idx)
{
    if((idx < -1) || (idx >= irrigationZoneCfgElements)) {
        return ERR_INVALID_PARAM;
    }

    eventData.zoneIdx = idx;
    return ERR_OK;
}

void IrrigationEvent::setDuration(unsigned int secs)
{
    eventData.durationSecs = secs;
}

void IrrigationEvent::setStartFlag(bool isStart)
{
    eventData.isStart = isStart;
}

IrrigationEvent::err_t IrrigationEvent::getEventData(irrigation_event_data_t* dest)
{
    if(nullptr == dest) return ERR_INVALID_PARAM;

    memcpy(dest, &eventData, sizeof(irrigation_event_data_t));
    return ERR_OK;
}

IrrigationEvent::err_t IrrigationEvent::setSingleEvent(int hour, int minute, int second,
    int day, int month, int year)
{
    err_t ret = ERR_OK;

    if((hour < 0) || (hour > 23)) ret = ERR_INVALID_TIME;
    if((minute < 0) || (minute > 59)) ret = ERR_INVALID_TIME;
    if((second < 0) || (second > 59)) ret = ERR_INVALID_TIME;
    if((day < 1) || (day > 31)) ret = ERR_INVALID_TIME;
    if((month < 1) || (month > 12)) ret = ERR_INVALID_TIME;
    if(year < 1900) ret = ERR_INVALID_TIME;

    if(ERR_OK == ret) {
        eventTime.tm_hour = hour;
        eventTime.tm_min = minute;
        eventTime.tm_sec = second;
        eventTime.tm_mday = day;
        eventTime.tm_mon = month - 1;
        eventTime.tm_year = year - 1900;
        eventTime.tm_isdst = -1;

        repetitionType = SINGLE;
    }

    return ret;
}

IrrigationEvent::err_t IrrigationEvent::setDailyRepetition(int hour, int minute, int second)
{
    err_t ret = ERR_OK;

    if((hour < 0) || (hour > 23)) ret = ERR_INVALID_TIME;
    if((minute < 0) || (minute > 59)) ret = ERR_INVALID_TIME;
    if((second < 0) || (second > 59)) ret = ERR_INVALID_TIME;

    if(ERR_OK == ret) {
        eventTime.tm_hour = hour;
        eventTime.tm_min = minute;
        eventTime.tm_sec = second;

        repetitionType = DAILY;
    }

    return ret;
}

/**
 * @brief Update the reference time for this event.
 * 
 * The reference time is used for comparing repetitive events against each other
 * and to calculate the next occurance of such events.
 * 
 * @param ref New reference time to be set.
 */
void IrrigationEvent::updateReferenceTime(time_t ref)
{
    refTime = ref;
}

/**
 * @brief Get the reference time for this event.
 * 
 * @return time_t Current reference time.
 */
time_t IrrigationEvent::getReferenceTime(void)
{
    return refTime;
}

/**
 * @brief Get the next occurance of this event based on the set reference time.
 * 
 * Note: If an event has exactly the same time as the reference, it will be reported
 * as the next occurance, i.e. it will not be reported for the following day/week/month.
 * 
 * @return time_t Time of next occurance.
 */
time_t IrrigationEvent::getNextOccurance(void) const
{
    time_t next = 0;

    struct tm refTm;
    struct tm nextTm;
    struct tm eventTm = eventTime; // make a copy to keep the underlying object const

    if(repetitionType == SINGLE) {
        next = mktime(&eventTm);
    }
    else if(repetitionType == DAILY) {
        // Convert reference time_t for easier handling
        localtime_r(&refTime, &refTm);

        // Make a full copy of it
        memcpy(&nextTm, &refTm, sizeof(struct tm));
        // ... and replace h:m:s
        nextTm.tm_hour = eventTm.tm_hour;
        nextTm.tm_min = eventTm.tm_min;
        nextTm.tm_sec = eventTm.tm_sec;

        // Adjust day in case event has already passed today
        // Note: mktime below will fixup month/hour/... overflows
        uint32_t refDaySecs = refTm.tm_hour*60*60 + refTm.tm_min*60 + refTm.tm_sec;
        uint32_t nextDaySecs = nextTm.tm_hour*60*60 + nextTm.tm_min*60 + nextTm.tm_sec;
        if(refDaySecs > nextDaySecs) {
            nextTm.tm_mday++;
        }

        // Set DST status to not available, because it may be different by 
        // the modifications modifications
        nextTm.tm_isdst = -1;

        next = mktime(&nextTm);
    }

    if(next < refTime) next = 0; // don't return events in the past
    return next;
}

/**
 * @brief Implementation of the 'equality' operator.
 * All operators are based on the event's time info only. The configuration
 * is completetly left out of the decision.
 */
bool IrrigationEvent::operator==(const IrrigationEvent& rhs) const
{
    return (getNextOccurance() == rhs.getNextOccurance());
}

/**
 * @brief Implementation of the 'in-equality' operator.
 */
bool IrrigationEvent::operator!=(const IrrigationEvent& rhs) const
{
    return (getNextOccurance() != rhs.getNextOccurance());
}

/**
 * @brief Implementation of the 'less than' operator.
 */
bool IrrigationEvent::operator<(const IrrigationEvent& rhs) const
{
    time_t us_time = getNextOccurance();

    if(us_time != 0) {
        return (getNextOccurance() < rhs.getNextOccurance());
    } else {
        // If we are invalid, report us as greater, because this will interfere the least with irrigation planning
        return false;
    }
}

/**
 * @brief Implementation of the 'less than or equal' operator.
 */
bool IrrigationEvent::operator<=(const IrrigationEvent& rhs) const
{
    time_t us_time = getNextOccurance();

    if(us_time != 0) {
        return (getNextOccurance() <= rhs.getNextOccurance());
    } else {
        // If we are invalid, report us as greater, because this will interfere the least with irrigation planning
        return false;
    }
}

/**
 * @brief Implementation of the 'greater than' operator.
 */
bool IrrigationEvent::operator>(const IrrigationEvent& rhs) const
{
    time_t us_time = getNextOccurance();

    if(us_time != 0) {
        return (getNextOccurance() > rhs.getNextOccurance());
    } else {
        // If we are invalid, report us as greater, because this will interfere the least with irrigation planning
        return true;
    }
}

/**
 * @brief Implementation of the 'greater than or equal' operator.
 */
bool IrrigationEvent::operator>=(const IrrigationEvent& rhs) const
{
    time_t us_time = getNextOccurance();

    if(us_time != 0) {
        return (getNextOccurance() >= rhs.getNextOccurance());
    } else {
        // If we are invalid, report us as greater, because this will interfere the least with irrigation planning
        return true;
    }
}
