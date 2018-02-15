#ifndef IRRIGATION_EVENT_H
#define IRRIGATION_EVENT_H

#include <stdint.h>
#include <cstring> /* used for memcpy */
#include <ctime>
#include <vector>

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
        ERR_NO_CH_CFG = -3,
    } err_t;

    IrrigationEvent(void);
    ~IrrigationEvent(void);

    void addChannelConfig(uint32_t chNum, bool switchOn);
    unsigned int getChannelConfigSize(void);
    err_t getChannelConfigInfo(unsigned int num, uint32_t* chNum, bool* switchOn);

    err_t setSingleEvent(int hour, int minute, int second, int day, int month, int year);
    err_t setDailyRepetition(int hour, int minute, int second);
    //void setWeeklyRepetition();
    //void setMonthlyRepetition();

    void updateReferenceTime(time_t ref);
    time_t getNextOccurance(void) const;

    bool operator==(const IrrigationEvent &rhs) const;
    bool operator!=(const IrrigationEvent &rhs) const;
    bool operator<(const IrrigationEvent &rhs) const;
    bool operator<=(const IrrigationEvent &rhs) const;
    bool operator>(const IrrigationEvent &rhs) const;
    bool operator>=(const IrrigationEvent &rhs) const;

private:
    typedef struct ch_cfg {
        uint32_t chNum;
        bool switchOn;
    } ch_cfg_t;

    typedef enum {
        NOT_SET = 0,
        SINGLE = 1,
        DAILY = 2,
        WEEKLY = 3,
        MONTHLY = 4,
    } repetition_type_t;

    std::vector<ch_cfg_t> chCfg; /**< Vector to store channel configurations associated to this event */
    repetition_type_t repetitionType; /**< Stores the repetition type of this event */
    struct tm eventTime; /**< Stores the time info of this event. Note: Its fields are only sparsely used. */

    time_t refTime; /**< Stores the reference time for time comparisions and the next occurance. */
};

#endif /* IRRIGATION_EVENT_H */
