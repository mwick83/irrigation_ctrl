#ifndef WIFI_EVENTS_H
#define WIFI_EVENTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "freertos/event_groups.h"

extern EventGroupHandle_t wifiEvents;
extern const int wifiEventConnected;
extern const int wifiEventDisconnected;

#ifdef __cplusplus
}
#endif

#endif /* WIFI_EVENTS_H */
