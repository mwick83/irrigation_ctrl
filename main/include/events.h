#ifndef EVENTS_H
#define EVENTS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    EVENT_EMPTY                = 0x00,
    EVENT_HEARTBEAT            = 0x01,
    EVENT_CONSOLE_PROCESS      = 0x02,
} event_types_e;

#ifdef __cplusplus
}
#endif

#endif /*EVENTS_H*/
