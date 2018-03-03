#ifndef TIME_SYSTEM_H
#define TIME_SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    TIMESYSTEM_TIME_SET = 0,
} time_system_event_t;

typedef void(*TimeSystem_HookFncPtr)(void*, time_system_event_t);


void TimeSystem_Init(void);

void TimeSystem_GetCurTimeStr(char *timeStr);
int TimeSystem_SetTime(int16_t day, int16_t month, int16_t year, int16_t hour, int16_t minute, int16_t second);

void TimeSystem_RegisterHook(TimeSystem_HookFncPtr hook, void* param);

bool TimeSystem_TimeIsSet(void);
bool TimeSystem_WaitTimeSet(int waitMillis);
void TimeSystem_LogTime(void);

void TimeSystem_SntpStart(void);
void TimeSystem_SntpStop(void);
void TimeSystem_SntpRequest(void);

#ifdef __cplusplus
}
#endif

#endif /* TIME_SYSTEM_H */
