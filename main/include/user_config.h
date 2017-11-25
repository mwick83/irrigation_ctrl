#ifndef USER_CONFIG_H
#define USER_CONFIG_H

// global config
#define DEBUG

// debug log tags
static const char* LOG_TAG_WIFI __attribute__((unused)) = "wifi_app";
static const char* LOG_TAG_MQTT_CB __attribute__((unused)) = "mqtt_cb";
static const char* LOG_TAG_MAIN_CFG __attribute__((unused)) = "main_cfg";
static const char* LOG_TAG_TIME __attribute__((unused)) = "time";

// debug output config
//#define INFO(fmt, ...) os_printf("%s:%d::" fmt "\n", __BASE_FILE__, __LINE__, ##__VA_ARGS__)


#include "hardwareConfig.h"
#include "networkConfig.h"

#endif /* USER_CONFIG_H */
