#ifndef USER_CONFIG_H
#define USER_CONFIG_H

// global config
#define DEBUG

// debug log tags
static const char* LOG_TAG_WIFI __attribute__((unused)) = "wifi_app";
static const char* LOG_TAG_OTA __attribute__((unused)) = "ota";
static const char* LOG_TAG_SPIFFS __attribute__((unused)) = "spiffs";
static const char* LOG_TAG_MQTT_CFG_SETUP __attribute__((unused)) = "mqtt_cfg_setup";

// debug output config
//#define INFO(fmt, ...) os_printf("%s:%d::" fmt "\n", __BASE_FILE__, __LINE__, ##__VA_ARGS__)


#include "hardwareConfig.h"
#include "networkConfig.h"
#include "fileConfig.h"

#endif /* USER_CONFIG_H */
