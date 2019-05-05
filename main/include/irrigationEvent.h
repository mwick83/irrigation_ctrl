#ifndef IRRIGATION_EVENT_H
#define IRRIGATION_EVENT_H

#include <stdint.h>
#include <cstring> /* used for memcpy */
#include <ctime>
#include <vector>

#include "irrigationZoneCfg.h"

/**
 * @brief The IrrigationEvent class is a utility class to represent irrigation events.
 * It is used by the IrrigationPlanner and provides operators for easy event time
 * comparison. It also contains properties to decided which actions must be performed.
 */
class IrrigationEvent
{
public:
    typedef enum {
        ERR_OK = 0,
        ERR_INVALID_TIME = -1,
        ERR_INVALID_PARAM = -2,
    } err_t;

    typedef struct irrigation_event_data_t {
        irrigation_zone_cfg_t* zoneConfig;          /**< Pointer to the associated zone configuration */
        unsigned int           durationSecs;        /**< Stores the duration the channel configuration shall be kept active */
        bool                   isStart;             /**< Wether or not this is an irrigation start event */
        IrrigationEvent*       parentPtr;           /**< Pointer to the parent object containing the event data */
    } irrigation_event_data_t;

    IrrigationEvent(void);
    ~IrrigationEvent(void);

    void setDuration(unsigned int secs);
    void setZoneConfig(irrigation_zone_cfg_t* cfg);
    void setStartFlag(bool isStart);
    err_t getEventData(irrigation_event_data_t* dest);

    err_t setSingleEvent(int hour, int minute, int second, int day, int month, int year);
    err_t setDailyRepetition(int hour, int minute, int second);
    //void setWeeklyRepetition();
    //void setMonthlyRepetition();

    void updateReferenceTime(time_t ref);
    time_t getReferenceTime(void);
    time_t getNextOccurance(void) const;

    bool operator==(const IrrigationEvent& rhs) const;
    bool operator!=(const IrrigationEvent& rhs) const;
    bool operator<(const IrrigationEvent& rhs) const;
    bool operator<=(const IrrigationEvent& rhs) const;
    bool operator>(const IrrigationEvent& rhs) const;
    bool operator>=(const IrrigationEvent& rhs) const;

private:
    typedef enum {
        NOT_SET = 0,
        SINGLE = 1,
        DAILY = 2,
        WEEKLY = 3,
        MONTHLY = 4,
    } repetition_type_t;

    repetition_type_t repetitionType;               /**< Stores the repetition type of this event */
    irrigation_event_data_t eventData;              /**< Stores associated event data */
    struct tm eventTime;                            /**< Stores the time info of this event. Note: Its fields are only sparsely used. */

    time_t refTime;                                 /**< Stores the reference time for time comparisions and the next occurance */
};

#endif /* IRRIGATION_EVENT_H */
