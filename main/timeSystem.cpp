#include "timeSystem.h"

#include <cstdio>
#include <ctime>
#include <sys/time.h>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"

#include "apps/sntp/sntp.h"

#include "wifiEvents.h"


// ********************************************************************
// private objects, vars and prototypes
// ********************************************************************
static const char* LOG_TAG_TIME = "time";

static EventGroupHandle_t timeEvents;
const int TimeSystem_timeEventTimeSet = (1<<0);
const int TimeSystem_timeEventTimeSetSntp = (1<<1);

RTC_DATA_ATTR static time_t sntpLastSync = 0;
RTC_DATA_ATTR static time_t sntpNextSync = 0;

std::vector<TimeSystem_HookFncPtr> TimeSystem_Hooks;
std::vector<void*> TimeSystem_HookParamPtrs;
static void TimeSystem_CallHooks(time_system_event_t event);

SemaphoreHandle_t TimeSystem_HookMutex;
StaticSemaphore_t TimeSystem_HookMutexBuf;

// ********************************************************************
// SNTP setsystime overrides (requires patched ESP-IDF!)
// ********************************************************************
#ifdef __cplusplus
extern "C" {
#endif
void sntp_setsystemtime_us(time_t t, suseconds_t us)
{
    SNTP_SET_SYSTEM_TIME_US(t, us);
    sntpLastSync = t;

    ESP_LOGI(LOG_TAG_TIME, "Time set via SNTP. Setting timeEvents.");
    xEventGroupSetBits(timeEvents, TimeSystem_timeEventTimeSet | TimeSystem_timeEventTimeSetSntp);
    TimeSystem_CallHooks(TimeSystem_timeEventTimeSet | TimeSystem_timeEventTimeSetSntp);
    TimeSystem_LogTime();
}

void sntp_setsystemtime(time_t t)
{
    SNTP_SET_SYSTEM_TIME(t);
    sntpLastSync = t;

    ESP_LOGI(LOG_TAG_TIME, "Time set via SNTP. Setting timeEvents.");
    xEventGroupSetBits(timeEvents, TimeSystem_timeEventTimeSet | TimeSystem_timeEventTimeSetSntp);
    TimeSystem_CallHooks(TimeSystem_timeEventTimeSet | TimeSystem_timeEventTimeSetSntp);
    TimeSystem_LogTime();
}
#ifdef __cplusplus
}
#endif

// ********************************************************************
// time system initialization
// ********************************************************************
extern "C" void TimeSystem_Init(void)
{
    time_t now;
    struct tm timeinfo;

    timeEvents = xEventGroupCreate();
    TimeSystem_Hooks.clear();
    TimeSystem_HookParamPtrs.clear();

    TimeSystem_HookMutex = xSemaphoreCreateMutexStatic(&TimeSystem_HookMutexBuf);

    ESP_LOGI(LOG_TAG_TIME, "Checking if time is already set.");
    time(&now);

    // set correct timezone
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    localtime_r(&now, &timeinfo);

    // Is time set? If not, tm_year will be (1970 - 1900).
    if(!(timeinfo.tm_year < (2017 - 1900))) {
        ESP_LOGI(LOG_TAG_TIME, "-> Time already set. Setting timeEvents.");
        xEventGroupSetBits(timeEvents, TimeSystem_timeEventTimeSet);
        TimeSystem_CallHooks(TimeSystem_timeEventTimeSet);
        TimeSystem_LogTime();
    } else {
        ESP_LOGI(LOG_TAG_TIME, "-> Time not set.");
    }
}

// ********************************************************************
// time getters and setters
// ********************************************************************
extern "C" void TimeSystem_GetCurTimeStr(char *timeStr)
{
    if(NULL == timeStr) return;

    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(timeStr, 20, "%Y-%m-%d %H:%M:%S", &timeinfo);
}

extern "C" int TimeSystem_SetTime(int16_t day, int16_t month, int16_t year, int16_t hour, int16_t minute, int16_t second)
{
    int result = -1;

    struct timeval tv;
    struct tm tm;
    time_t t;

    if((day > 31) || (day < 1)) return -1;
    if((month > 12) || (month < 1)) return -1;
    if((year < 1970)) return -1;
    if((hour > 23) || (hour < 0)) return -1;
    if((minute > 59) || (minute < 0)) return -1;
    if((second > 59) || (second < 0)) return -1;

    tm.tm_mday = day;
    tm.tm_mon = month - 1;
    tm.tm_year = year - 1900;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = -1;

    t = mktime(&tm);
    tv.tv_sec = t;
    tv.tv_usec = 0;

    result = settimeofday(&tv, NULL);

    if(0 == result) {
        ESP_LOGI(LOG_TAG_TIME, "Time set. Setting timeEvents.");
        xEventGroupClearBits(timeEvents, TimeSystem_timeEventTimeSetSntp);
        xEventGroupSetBits(timeEvents, TimeSystem_timeEventTimeSet);
        TimeSystem_CallHooks(TimeSystem_timeEventTimeSet);
        TimeSystem_LogTime();
    } else {
        ESP_LOGE(LOG_TAG_TIME, "settimeofday failed with exit code %d.", result);
    }

    return result;
}

// ********************************************************************
// TimeSet hook handling
// ********************************************************************
void TimeSystem_CallHooks(time_system_event_t event)
{
    if(pdTRUE == xSemaphoreTake(TimeSystem_HookMutex, portMAX_DELAY)) {
        for(int i = 0; i < TimeSystem_Hooks.size(); i++) {
            TimeSystem_Hooks[i](TimeSystem_HookParamPtrs[i], event);
        }

        if(pdFALSE == xSemaphoreGive(TimeSystem_HookMutex)) {
            ESP_LOGE(LOG_TAG_TIME, "Error occurred releasing the TimeSystem_HookMutex.");
        }
    } else {
        ESP_LOGE(LOG_TAG_TIME, "Error occurred acquiring the TimeSystem_HookMutex.");
    }
}

void TimeSystem_RegisterHook(TimeSystem_HookFncPtr hook, void* param)
{
    if(pdTRUE == xSemaphoreTake(TimeSystem_HookMutex, portMAX_DELAY)) {
        TimeSystem_Hooks.push_back(hook);
        TimeSystem_HookParamPtrs.push_back(param);

        if(pdFALSE == xSemaphoreGive(TimeSystem_HookMutex)) {
            ESP_LOGE(LOG_TAG_TIME, "Error occurred releasing the TimeSystem_HookMutex.");
        }
    } else {
        ESP_LOGE(LOG_TAG_TIME, "Error occurred acquiring the TimeSystem_HookMutex.");
    }
}

// ********************************************************************
// misc helpers
// ********************************************************************
bool TimeSystem_TimeIsSet(void)
{
    return (0 != (xEventGroupGetBits(timeEvents) & TimeSystem_timeEventTimeSet));
}

bool TimeSystem_TimeIsSetSntp(void)
{
    return (0 != (xEventGroupGetBits(timeEvents) & TimeSystem_timeEventTimeSetSntp));
}

bool TimeSystem_WaitTimeSet(int waitMillis)
{
    TickType_t wait = portMAX_DELAY;
    EventBits_t events;

    if(waitMillis >= 0) {
        wait = pdMS_TO_TICKS(waitMillis);
    }

    events = xEventGroupWaitBits(timeEvents, TimeSystem_timeEventTimeSet, pdFALSE, pdTRUE, wait);
    if(0 != (events & TimeSystem_timeEventTimeSet)) {
        return true;
    } else {
        return false;
    }
}

bool TimeSystem_WaitTimeSetSntp(int waitMillis)
{
    TickType_t wait = portMAX_DELAY;
    EventBits_t events;

    if(waitMillis >= 0) {
        wait = pdMS_TO_TICKS(waitMillis);
    }

    events = xEventGroupWaitBits(timeEvents, TimeSystem_timeEventTimeSet | TimeSystem_timeEventTimeSetSntp, pdFALSE, pdTRUE, wait);
    if(0 != (events & (TimeSystem_timeEventTimeSet | TimeSystem_timeEventTimeSetSntp))) {
        return true;
    } else {
        return false;
    }
}

void TimeSystem_LogTime(void)
{
    static char strftime_buf[20];
    TimeSystem_GetCurTimeStr(strftime_buf);
    ESP_LOGI(LOG_TAG_TIME, "Current time: %s", strftime_buf);
}

time_t TimeSystem_GetLastSntpSync(void)
{
    return sntpLastSync;
}

time_t TimeSystem_GetNextSntpSync(void)
{
    return sntpNextSync;
}

void TimeSystem_SetNextSntpSync(time_t next)
{
    sntpNextSync = next;
}

// ********************************************************************
// SNTP handling
// ********************************************************************
void TimeSystem_SntpStart(void)
{
    if(0 != (xEventGroupGetBits(wifiEvents) & wifiEventConnected)) {
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, (char*) "de.pool.ntp.org");
        sntp_init();
    }
}

void TimeSystem_SntpStop(void)
{
    if(sntp_enabled()) {
        xEventGroupClearBits(timeEvents, TimeSystem_timeEventTimeSetSntp);
        sntp_stop();
    }
}

void TimeSystem_SntpRequest(void)
{
    TimeSystem_SntpStop();
    TimeSystem_SntpStart();
}
