#ifndef USER_CONFIG_H
#define USER_CONFIG_H

// global config
#define DEBUG

// debug log tags
static const char* LOG_TAG_WIFI __attribute__((unused)) = "wifi_app";
static const char* LOG_TAG_OTA __attribute__((unused)) = "ota";

// debug output config
//#define INFO(fmt, ...) os_printf("%s:%d::" fmt "\n", __BASE_FILE__, __LINE__, ##__VA_ARGS__)


#include "hardwareConfig.h"
#include "networkConfig.h"

#endif /* USER_CONFIG_H */
