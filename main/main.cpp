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

#include "apps/sntp/sntp.h"

#include "user_config.h"
#include "mqtt.h"
#include "serialPacketizer.h"
#include "fillSensorProtoHandler.h"
#include "console.h"
#include "powerManager.h"
//#include "WebServer.h"


// ********************************************************************
// global objects, vars and prototypes
// ********************************************************************
// event group to signal WiFi events
static EventGroupHandle_t wifiEvents;
const int wifiEventConnected = (1<<0);
const int wifiEventDisconnected = (1<<1);

static EventGroupHandle_t timeEvents;
const int timeSet = (1<<0);

mqtt_client* mqttClient;
mqtt_settings mqttSettings;

// MAC address
static uint8_t mac_addr[6];

// fill level sensor
FillSensorPacketizer* fillSensorPacketizer;
FillSensorProtoHandler<FillSensorPacketizer>* fillSensorProto;

// power manager
PowerManager pwrMgr;

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
            mqttClient = mqtt_start(&mqttSettings); // TBD: to main loop
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            /* This is a workaround as ESP32 WiFi libs don't currently
            auto-reassociate. */
            esp_wifi_connect();
            xEventGroupClearBits(wifiEvents, wifiEventConnected);
            xEventGroupSetBits(wifiEvents, wifiEventDisconnected);
            mqtt_stop(); // TBD: to main loop
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

// ********************************************************************
// MQTT callbacks + setup
// ********************************************************************
#define MQTT_OTA_UPGRADE_TOPIC_PRE              "whan/ota_upgrade/"
#define MQTT_OTA_UPGRADE_TOPIC_PRE_LEN          17
#define MQTT_OTA_UPGRADE_TOPIC_POST_REQ         "/req"
#define MQTT_OTA_UPGRADE_TOPIC_POST_REQ_LEN     4

#define MQTT_DIMMERS_TOPIC_PRE               	"whan/dimmers/"
#define MQTT_DIMMERS_TOPIC_PRE_LEN           	13
#define MQTT_DIMMERS_TOPIC_CH_WILDCARD          "/+"
#define MQTT_DIMMERS_TOPIC_CH_WILDCARD_LEN      2
#define MQTT_DIMMERS_TOPIC_POST_SET          	"/set"
#define MQTT_DIMMERS_TOPIC_POST_SET_LEN      	4
#define MQTT_DIMMERS_TOPIC_POST_STATE        	"/state"
#define MQTT_DIMMERS_TOPIC_POST_STATE_LEN    	6

#define MQTT_DIMMERS_STATE_DATA_LEN_MAX      	38
#define MQTT_DIMMERS_STATE_TOPIC_LEN_MAX		(MQTT_DIMMERS_TOPIC_PRE_LEN + 2 + MQTT_DIMMERS_TOPIC_POST_STATE_LEN + 12 + 1)

static char state_update_topic[MQTT_DIMMERS_STATE_TOPIC_LEN_MAX];
static char state_update_data[MQTT_DIMMERS_STATE_DATA_LEN_MAX];

void mqtt_connected_cb(mqtt_client* client, mqtt_event_data_t* event_data)
{
    int i;
    char *topic;

    ESP_LOGI(LOG_TAG_MQTT_CB, "Connected.");

    #if 0
    // prepare subscription topic
    topic = (char*) malloc(MQTT_DIMMERS_TOPIC_PRE_LEN + MQTT_DIMMERS_TOPIC_CH_WILDCARD_LEN + MQTT_DIMMERS_TOPIC_POST_SET_LEN + 12 + 1);
    memcpy(topic, MQTT_DIMMERS_TOPIC_PRE, MQTT_DIMMERS_TOPIC_PRE_LEN);
    for(i=0; i<6; i++) {
        sprintf(&topic[MQTT_DIMMERS_TOPIC_PRE_LEN + i*2], "%02x", mac_addr[i]);
    }

    // subscribe to set topic
    memcpy(&topic[MQTT_DIMMERS_TOPIC_PRE_LEN + 12], MQTT_DIMMERS_TOPIC_CH_WILDCARD, MQTT_DIMMERS_TOPIC_CH_WILDCARD_LEN);
    memcpy(&topic[MQTT_DIMMERS_TOPIC_PRE_LEN + MQTT_DIMMERS_TOPIC_CH_WILDCARD_LEN + 12], MQTT_DIMMERS_TOPIC_POST_SET, MQTT_DIMMERS_TOPIC_POST_SET_LEN);
    topic[MQTT_DIMMERS_TOPIC_PRE_LEN + MQTT_DIMMERS_TOPIC_CH_WILDCARD_LEN + MQTT_DIMMERS_TOPIC_POST_SET_LEN + 12] = 0;
    mqtt_subscribe(client, topic, 2);

    free(topic);

    // subscribe to OTA topic
    topic = (char*) malloc(MQTT_OTA_UPGRADE_TOPIC_PRE_LEN + MQTT_OTA_UPGRADE_TOPIC_POST_REQ_LEN + 12 + 1);
    memcpy(topic, MQTT_OTA_UPGRADE_TOPIC_PRE, MQTT_OTA_UPGRADE_TOPIC_PRE_LEN);
    for(i=0; i<6; i++) {
        sprintf(&topic[MQTT_OTA_UPGRADE_TOPIC_PRE_LEN + i*2], "%02x", mac_addr[i]);
    }
    memcpy(&topic[MQTT_OTA_UPGRADE_TOPIC_PRE_LEN + 12], MQTT_OTA_UPGRADE_TOPIC_POST_REQ, MQTT_OTA_UPGRADE_TOPIC_POST_REQ_LEN);
    topic[MQTT_OTA_UPGRADE_TOPIC_PRE_LEN + MQTT_OTA_UPGRADE_TOPIC_POST_REQ_LEN + 12] = 0;
    mqtt_subscribe(client, topic, 2);

    free(topic);
    #endif
}

void mqtt_disconnected_cb(mqtt_client* client, mqtt_event_data_t* event_data)
{
    ESP_LOGI(LOG_TAG_MQTT_CB, "Disconnected.");
}

void mqtt_published_cb(mqtt_client* client, mqtt_event_data_t* event_data)
{
    ESP_LOGI(LOG_TAG_MQTT_CB, "Published.");
}

void mqtt_data_cb(mqtt_client* client, mqtt_event_data_t* event_data)
{
    unsigned int topic_len = event_data->topic_length;
    unsigned int data_len = event_data->data_length;

    char *topicBuf = (char*) malloc(topic_len+1);
    char *dataBuf = (char*) malloc(data_len+1);

    if((NULL != topicBuf) && (NULL != dataBuf))
    {
        memcpy(topicBuf, event_data->topic, topic_len);
        topicBuf[topic_len] = 0;

        memcpy(dataBuf, event_data->data, data_len);
        dataBuf[data_len] = 0;

        ESP_LOGD(LOG_TAG_MQTT_CB, "topic: %s, data: %s", topicBuf, dataBuf);

        #if 0
        // check prefix of topic, but expect the MAC address to match
        if(strncmp(topicBuf, MQTT_DIMMERS_TOPIC_PRE, MQTT_DIMMERS_TOPIC_PRE_LEN) == 0) {
            // check length (pre+set + at least two more chars (xxx/0/set) and set command at the end
            if( (topic_len >= (MQTT_DIMMERS_TOPIC_PRE_LEN+MQTT_DIMMERS_TOPIC_POST_SET_LEN + 12 + 2)) &&
            (topicBuf[MQTT_DIMMERS_TOPIC_PRE_LEN+12] == '/') &&
            (strncmp(topicBuf+topic_len-MQTT_DIMMERS_TOPIC_POST_SET_LEN, MQTT_DIMMERS_TOPIC_POST_SET, MQTT_DIMMERS_TOPIC_POST_SET_LEN) == 0) ) {
                // try to parse channel number
                char* parseEndPtr = &topicBuf[MQTT_DIMMERS_TOPIC_PRE_LEN+12+1];
                long channel = strtol(&topicBuf[MQTT_DIMMERS_TOPIC_PRE_LEN+12+1], &parseEndPtr, 10);
                //long channel = 0;

                // check if conversion was successfull / non-empty and is within range
                if((parseEndPtr != &topicBuf[MQTT_DIMMERS_TOPIC_PRE_LEN+12+1]) && (channel < DIMMER_CHANNELS)) {
                    // parse JSON request inspired by https://home-assistant.io/components/light.mqtt_json/
                    const nx_json* msg_root = nx_json_parse_utf8(dataBuf);
                    if(!msg_root) {
                        ESP_LOGI(LOG_TAG_MQTT_CB, "Received unparsable JSON data on set topic!");
                    } else {
                        const nx_json* msg_state = nx_json_get(msg_root, "state");
                        if(msg_state && (msg_state->type == NX_JSON_STRING)) {
                            #if 0
                            ESP_LOGI(LOG_TAG_MQTT_CB, "set state: %s", msg_state->text_value);
                            #endif
                            if((strcmp(msg_state->text_value, "ON") == 0) || (strcmp(msg_state->text_value, "on") == 0)) {
                                dimmer_set_switch(channel, true);
                            } else if((strcmp(msg_state->text_value, "OFF") == 0) || (strcmp(msg_state->text_value, "off") == 0)) {
                                dimmer_set_switch(channel, false);
                            }
                        }
                        
                        const nx_json* msg_brightness = nx_json_get(msg_root, "brightness");
                        if(msg_brightness && (msg_brightness->type == NX_JSON_INTEGER)) {
                            #if 0
                            ESP_LOGI(LOG_TAG_MQTT_CB, "set dim: %d", (uint32_t) msg_brightness->int_value);
                            #endif
                            uint32_t dim = msg_brightness->int_value;
                            if((dim >= 0) && (dim <= 255)) {
                                dimmer_set_dim(channel, dim);
                            }
                        }
                        
                        nx_json_free(msg_root);
                    }
                } else {
                    ESP_LOGI(LOG_TAG_MQTT_CB, "Received unparsable channel number!");
                }
            }
        }
        // check prefix of OTA topic, but expect the MAC address to match
        else if(strncmp(topicBuf, MQTT_OTA_UPGRADE_TOPIC_PRE, MQTT_OTA_UPGRADE_TOPIC_PRE_LEN) == 0) {
            // check length and req command
            if( (topic_len == (MQTT_OTA_UPGRADE_TOPIC_PRE_LEN + MQTT_OTA_UPGRADE_TOPIC_POST_REQ_LEN + 12)) &&
            (strncmp(topicBuf+topic_len-MQTT_OTA_UPGRADE_TOPIC_POST_REQ_LEN, MQTT_OTA_UPGRADE_TOPIC_POST_REQ, MQTT_OTA_UPGRADE_TOPIC_POST_REQ_LEN) == 0) ) {
                ESP_LOGI(LOG_TAG_MQTT_CB, "OTA topic matched!");
                
                if(!ota_is_in_progress()) {
                    ota_start(dataBuf);
                }
            } else {
                ESP_LOGI(LOG_TAG_MQTT_CB, "Another OTA update is still in progress!");
            }
        }
        #endif
    } else {
        ESP_LOGW(LOG_TAG_MQTT_CB, "Couldn't allocate memory for topic/data buffer. Ignoring data.");
    }

    if(NULL != topicBuf) free(topicBuf);
    if(NULL != dataBuf) free(dataBuf);
}

#if 0
void mqtt_dimmer_state_update_cb_init(void)
{
    int i;

    memset(state_update_topic, 0, MQTT_DIMMERS_STATE_TOPIC_LEN_MAX);
    memset(state_update_data, 0, MQTT_DIMMERS_STATE_DATA_LEN_MAX);

    // prepare first part of state update topic (fix), last part will be set on demand
    memcpy(state_update_topic, MQTT_DIMMERS_TOPIC_PRE, MQTT_DIMMERS_TOPIC_PRE_LEN);
    for(i=0; i<6; i++) {
        sprintf(&state_update_topic[MQTT_DIMMERS_TOPIC_PRE_LEN + i*2], "%02x", mac_addr[i]);
    }
}

void mqtt_dimmer_state_update_cb(uint32_t ch, DIMMER_DATA_T state)
{
    int charsWritten = 0;

    // set last part of state_update_topic
    charsWritten = sprintf(&state_update_topic[MQTT_DIMMERS_TOPIC_PRE_LEN + 12], "/%u", ch);
    // sanity check if not too much data written
    if(charsWritten <= 2) {
        memcpy(&state_update_topic[MQTT_DIMMERS_TOPIC_PRE_LEN + 12 + charsWritten], MQTT_DIMMERS_TOPIC_POST_STATE, MQTT_DIMMERS_TOPIC_POST_STATE_LEN);
        state_update_topic[MQTT_DIMMERS_TOPIC_PRE_LEN + 12 + charsWritten + MQTT_DIMMERS_TOPIC_POST_STATE_LEN] = 0;

        sprintf(state_update_data, 
            "{ \"brightness\": %u, \"state\": \"%s\" }", 
            state.dim, (state.state ? "ON" : "OFF"));

        mqtt_publish(&mqttClient, state_update_topic, state_update_data, strlen(state_update_data), 2, 1);
    } else {
        ESP_LOGI(LOG_TAG_MQTT_CB, "State update topic got too long! Code config error?");
    }
}
#endif

esp_err_t mqtt_prepare_settings(void)
{
    int ret = ESP_OK;

    int i;
    size_t mqtt_client_id_len;
    char *clientName;

    mqtt_client_id_len = strlen(MQTT_CLIENT_ID) + 12;
    clientName = (char*) malloc(mqtt_client_id_len+1);

    if(NULL == clientName) ret = ESP_ERR_NO_MEM;
    if(strlen(MQTT_HOST) > CONFIG_MQTT_MAX_HOST_LEN) ret = ESP_ERR_INVALID_ARG;
    if(strlen(MQTT_USER) > CONFIG_MQTT_MAX_USERNAME_LEN) ret = ESP_ERR_INVALID_ARG;
    if(strlen(MQTT_PASS) > CONFIG_MQTT_MAX_PASSWORD_LEN) ret = ESP_ERR_INVALID_ARG;
    if(mqtt_client_id_len > CONFIG_MQTT_MAX_CLIENT_LEN) ret = ESP_ERR_INVALID_ARG;

    if(ESP_OK == ret) {
        // prepare MQTT client name (prefix + mac_address)
        memcpy(clientName, MQTT_CLIENT_ID, mqtt_client_id_len-12);
        for(i=0; i<6; i++) {
            sprintf(&clientName[mqtt_client_id_len-12+i*2], "%02x", mac_addr[i]);
        }
        clientName[mqtt_client_id_len] = 0;

        // prepare MQTT settings
        strncpy(mqttSettings.host, MQTT_HOST, CONFIG_MQTT_MAX_HOST_LEN);
        mqttSettings.port = MQTT_PORT;
        strncpy(mqttSettings.username, MQTT_USER, CONFIG_MQTT_MAX_USERNAME_LEN);
        strncpy(mqttSettings.password, MQTT_PASS, CONFIG_MQTT_MAX_PASSWORD_LEN);
        mqttSettings.clean_session = 0;
        mqttSettings.keepalive = MQTT_KEEPALIVE;
        strncpy(mqttSettings.client_id, clientName, CONFIG_MQTT_MAX_CLIENT_LEN);
        mqttSettings.auto_reconnect = true;
        mqttSettings.lwt_topic[0] = 0;
        mqttSettings.connected_cb = mqtt_connected_cb;
        mqttSettings.disconnected_cb = mqtt_disconnected_cb;
        mqttSettings.subscribe_cb = NULL;
        mqttSettings.publish_cb = mqtt_published_cb;
        mqttSettings.data_cb = mqtt_data_cb;
    }

    if(NULL != clientName) free(clientName);

    return ret;
}

// ********************************************************************
// sntp_task
// ********************************************************************
void sntp_task(void* params)
{
    char strftime_buf[64];

    ESP_LOGI(LOG_TAG_TIME, "Checking if time is already set.");
    time_t now;
    struct tm timeinfo;
    time(&now);

    // set correct timezone
    setenv("TZ", "CET-1CEST", 1);
    tzset();
    localtime_r(&now, &timeinfo);

    // Is time set? If not, tm_year will be (1970 - 1900).
    if(!(timeinfo.tm_year < (2017 - 1900))) {
        ESP_LOGI(LOG_TAG_TIME, "-> Time already set. Setting timeEvents.");
        xEventGroupSetBits(timeEvents, timeSet);
    } else {
        ESP_LOGI(LOG_TAG_TIME, "-> Time not set.");
    }

    while(1) {
        // wait being online
        xEventGroupWaitBits(wifiEvents, wifiEventConnected, false, true, portMAX_DELAY);

        ESP_LOGI(LOG_TAG_TIME, "WiFi connect detected. Initializing SNTP.");
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, (char*) "de.pool.ntp.org");
        sntp_init();

        while(timeinfo.tm_year < (2017 - 1900)) {
            ESP_LOGI(LOG_TAG_TIME, "Waiting for system time to be set.");
            vTaskDelay(pdMS_TO_TICKS(2000));
            time(&now);
            localtime_r(&now, &timeinfo);
        }
        ESP_LOGI(LOG_TAG_TIME, "Time set. Setting timeEvents.");
        xEventGroupSetBits(timeEvents, timeSet);

        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(LOG_TAG_TIME, "Current time: %s", strftime_buf);

        // wait for potential connection loss
        xEventGroupWaitBits(wifiEvents, wifiEventDisconnected, false, true, portMAX_DELAY);

        ESP_LOGI(LOG_TAG_TIME, "WiFi disconnect detected. Stopping SNTP.");
        sntp_stop();
    }

    vTaskDelete(NULL);
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
    TaskHandle_t sntpTaskHandle;

    //uint8_t fillLevelReqData[1] = {0x01};
    int fillLevel;
    float battVoltage;

    timeEvents = xEventGroupCreate();

    ESP_ERROR_CHECK( nvs_flash_init() );

    // Initialize WiFi, but don't start yet.
    initialiseWifi();

    // Store MAC addr globally to use in mqtt callbacks
    ESP_ERROR_CHECK( esp_wifi_get_mac(ESP_IF_WIFI_STA, mac_addr) );

    // Prepare global mqtt settings (needed due to lack of named initializers in C99).
    ESP_ERROR_CHECK( mqtt_prepare_settings() );

    // Start WiFi. Events will start/stop MQTT client
    ESP_ERROR_CHECK( esp_wifi_start() );

    // Start NTP task, TBD: use static task allocation xTaskCreateStatic
    if(pdPASS == xTaskCreate(sntp_task, "sntp_task", 2048, (void*) NULL, tskIDLE_PRIORITY+1, &sntpTaskHandle)) {
        ESP_LOGI(LOG_TAG_TIME, "SNTP task created. Starting.");
    } else {
        ESP_LOGE(LOG_TAG_TIME, "SNTP task creation failed!");
    }

    fillSensorPacketizer = new FillSensorPacketizer();
    fillSensorProto = new FillSensorProtoHandler<FillSensorPacketizer>(fillSensorPacketizer);

    ConsoleInit(true, ConsoleStartHook, ConsoleExitHook);

    while(1) {
        //fillLevel = fillSensorProto->getFillLevel();
        //ESP_LOGI(LOG_TAG_MAIN_CFG, "Fill level: %d", fillLevel);

        battVoltage = pwrMgr.getSupplyVoltageMilli();
        ESP_LOGI(LOG_TAG_MAIN_CFG, "Batt voltage: %02.2f V", roundf(battVoltage * 0.1f) * 0.01f);

        if(((xEventGroupGetBits(timeEvents) & timeSet) == 0) || !pwrMgr.gotoSleep()) {
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
}
