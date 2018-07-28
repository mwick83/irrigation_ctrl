#include <stdio.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "user_config.h"
#include "mqtt_client.h"
#include "console.h"
#include "wifiEvents.h"
#include "globalComponents.h"
#include "irrigationController.h"
#include "irrigationPlanner.h"


// ********************************************************************
// global objects, vars and prototypes
// ********************************************************************
// TBD: encapsulate Wifi events/system/handling
// event group to signal WiFi events
EventGroupHandle_t wifiEvents;
const int wifiEventConnected = (1<<0);
const int wifiEventDisconnected = (1<<1);

FillSensorPacketizer fillSensorPacketizer;
FillSensorProtoHandler<FillSensorPacketizer> fillSensor(&fillSensorPacketizer);
PowerManager pwrMgr;
OutputController outputCtrl;
MqttManager mqttMgr;
IrrigationController irrigCtrl;
IrrigationPlanner irrigPlanner;

// ********************************************************************
// WiFi handling
// ********************************************************************
static esp_err_t wifiEventHandler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifiEvents, wifiEventConnected);
            xEventGroupClearBits(wifiEvents, wifiEventDisconnected);
            mqttMgr.start();
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            xEventGroupClearBits(wifiEvents, wifiEventConnected);
            xEventGroupSetBits(wifiEvents, wifiEventDisconnected);
            mqttMgr.stop();
            TimeSystem_SntpStop();
            /* This is a workaround as ESP32 WiFi libs don't currently auto-reassociate. */
            esp_wifi_connect();
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void initialiseWifi(void)
{
    ESP_LOGI(LOG_TAG_WIFI, "Initializing WiFi.");

    tcpip_adapter_init();

    wifiEvents = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(wifiEventHandler, NULL) );

    static wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );

    static wifi_config_t wifiConfig;
    memcpy(wifiConfig.sta.ssid, STA_SSID, sizeof(STA_SSID) / sizeof(uint8_t));
    memcpy(wifiConfig.sta.password, STA_PASS, sizeof(STA_PASS) / sizeof(uint8_t));

    ESP_LOGI(LOG_TAG_WIFI, "Setting WiFi configuration for SSID %s.", wifiConfig.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifiConfig) );
}

esp_err_t initializeMqttMgr(void)
{
    static uint8_t mac_addr[6];

    int ret = ESP_OK;

    int i;
    size_t mqtt_client_id_len;
    char *clientName;

    mqtt_client_id_len = strlen(MQTT_CLIENT_ID) + 12;
    clientName = (char*) malloc(mqtt_client_id_len+1);

    if(nullptr == clientName) ret = ESP_ERR_NO_MEM;
    if(mqtt_client_id_len > MQTT_MAX_CLIENT_LEN) ret = ESP_ERR_INVALID_ARG;

    if(ESP_OK == ret) {
        ret = esp_wifi_get_mac(ESP_IF_WIFI_STA, mac_addr);
    }

    if(ESP_OK == ret) {
        // prepare MQTT client name (prefix + mac_address)
        memcpy(clientName, MQTT_CLIENT_ID, mqtt_client_id_len-12);
        for(i=0; i<6; i++) {
            sprintf(&clientName[mqtt_client_id_len-12+i*2], "%02x", mac_addr[i]);
        }
        clientName[mqtt_client_id_len] = 0;

        bool ssl = false;
        #if defined(MQTT_SECURITY) && (MQTT_SECURITY == 1)
        ssl = true;
        #endif
        mqttMgr.init(MQTT_HOST, MQTT_PORT, ssl, MQTT_USER, MQTT_PASS, clientName);
    }

    if(nullptr != clientName) free(clientName);

    return ret;
}


// ********************************************************************
// app_main
// ********************************************************************
//static vprintf_like_t previousLogVprintf;
void ConsoleStartHook(void)
{
    esp_log_level_set("*", ESP_LOG_WARN);
    // alternative: previousLogVprintf = esp_log_set_vprintf();
}

void ConsoleExitHook(void)
{
    esp_log_level_set("*", (esp_log_level_t) CONFIG_LOG_DEFAULT_LEVEL);
    // alternaive: esp_log_set_vprintf(previousLogVprintf);
}

extern "C" void app_main()
{
    // Begin with system init now.
    ESP_ERROR_CHECK( nvs_flash_init() );

    // Initialize WiFi, but don't start yet.
    initialiseWifi();

    // Prepare global mqtt clientName (needed due to lack of named initializers in C99) 
    // and init the manager.
    ESP_ERROR_CHECK( initializeMqttMgr() );

    // Start WiFi. Events will start/stop MQTT client
    ESP_ERROR_CHECK( esp_wifi_start() );

    // Initialize the time system
    TimeSystem_Init();

    ConsoleInit(true, ConsoleStartHook, ConsoleExitHook);

    irrigCtrl.start();
}
