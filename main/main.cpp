#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"

#include "user_config.h"
#include "version.h"
#include "mqtt_client.h"
#include "console.h"
#include "wifiEvents.h"
#include "globalComponents.h"
#include "irrigationController.h"
#include "irrigationPlanner.h"
#include "iap_https.h"
#include "cJSON.h"

#define MIN(a, b) ((a <= b) ? a : b)

#ifndef OTA_METADATA_FILE
#define OTA_METADATA_FILE "unknown.bin"
#endif

// ********************************************************************
// global objects, vars and prototypes
// ********************************************************************
// TBD: encapsulate Wifi events/system/handling
// event group to signal WiFi events
EventGroupHandle_t wifiEvents;
const int wifiEventConnected = (1<<0);
const int wifiEventDisconnected = (1<<1);

SettingsManager settingsMgr;
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

    // delegate events to OTA subsystem
    iap_https_wifi_sta_event_callback(event);

    return ESP_OK;
}

static void initializeWifi(void)
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

    esp_err_t ret = ESP_OK;

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
        mqttMgr.init(MQTT_HOST, MQTT_PORT, ssl, MQTT_USER, MQTT_PASS, clientName, true, mqttReconnectTimeoutMs);
    }

    if(nullptr != clientName) free(clientName);

    return ret;
}

// ********************************************************************
// OTA
// ********************************************************************
extern const uint8_t ota_root_ca_cert_pem_start[] asm("_binary_ota_root_ca_cert_pem_start");
extern const uint8_t ota_root_ca_cert_pem_end[] asm("_binary_ota_root_ca_cert_pem_end");
extern const uint8_t ota_host_public_key_pem_start[] asm("_binary_ota_host_public_key_pem_start");
extern const uint8_t ota_host_public_key_pem_end[] asm("_binary_ota_host_public_key_pem_end");

#define MQTT_OTA_UPGRADE_TOPIC_PRE              "whan/ota_upgrade/"
#define MQTT_OTA_UPGRADE_TOPIC_PRE_LEN          17
#define MQTT_OTA_UPGRADE_TOPIC_POST_REQ         "/req"
#define MQTT_OTA_UPGRADE_TOPIC_POST_REQ_LEN     4
void mqttOtaCallback(const char* topic, int topicLen, const char* data, int dataLen);
void iapHttpsEventCallback(iap_https_event_t* event);

static void initializeOta()
{
    static iap_https_config_t ota_config;

    ESP_LOGI(LOG_TAG_OTA, "Initialising OTA firmware updater.");

    ota_config.current_software_version = OTA_VERSION_STRING;
    ota_config.server_host_name = OTA_HOST;
    ota_config.server_port = OTA_PORT;
    strncpy(ota_config.server_metadata_path, OTA_METADATA_FILE, sizeof(ota_config.server_metadata_path) / sizeof(char));
    bzero(ota_config.server_firmware_path, sizeof(ota_config.server_firmware_path) / sizeof(char));
    ota_config.server_root_ca_public_key_pem = (const char*) ota_root_ca_cert_pem_start;
    ota_config.server_root_ca_public_key_pem_len = ota_root_ca_cert_pem_end - ota_root_ca_cert_pem_start;
    ota_config.peer_public_key_pem = (const char*) ota_host_public_key_pem_start;
    ota_config.peer_public_key_pem_len = ota_host_public_key_pem_end - ota_host_public_key_pem_start;
    ota_config.polling_interval_s = OTA_POLLING_INTERVAL_S;
    ota_config.auto_reboot = 0;
    ota_config.event_callback = iapHttpsEventCallback;

    iap_https_init(&ota_config);

    // subscribe to OTA topic
    static char otaTopic[MQTT_OTA_UPGRADE_TOPIC_PRE_LEN + MQTT_OTA_UPGRADE_TOPIC_POST_REQ_LEN + 12 + 1];
    static uint8_t mac_addr[6];
    int i;
    esp_err_t ret;

    ret = esp_wifi_get_mac(ESP_IF_WIFI_STA, mac_addr);

    if(ESP_OK == ret) {
        memcpy(otaTopic, MQTT_OTA_UPGRADE_TOPIC_PRE, MQTT_OTA_UPGRADE_TOPIC_PRE_LEN);
        for(i=0; i<6; i++) {
            sprintf(&otaTopic[MQTT_OTA_UPGRADE_TOPIC_PRE_LEN + i*2], "%02x", mac_addr[i]);
        }
        memcpy(&otaTopic[MQTT_OTA_UPGRADE_TOPIC_PRE_LEN + 12], MQTT_OTA_UPGRADE_TOPIC_POST_REQ, MQTT_OTA_UPGRADE_TOPIC_POST_REQ_LEN);
        otaTopic[MQTT_OTA_UPGRADE_TOPIC_PRE_LEN + MQTT_OTA_UPGRADE_TOPIC_POST_REQ_LEN + 12] = 0;
        
        if(MqttManager::ERR_OK != mqttMgr.subscribe(otaTopic, MqttManager::QOS_EXACTLY_ONCE, mqttOtaCallback)) {
            ESP_LOGW(LOG_TAG_OTA, "Failed to subscribe to OTA topic!");
        }
    } else {
        ESP_LOGW(LOG_TAG_OTA, "Failed to get WiFi MAC address for OTA topic subscription.");
    }
}

void mqttOtaCallback(const char* topic, int topicLen, const char* data, int dataLen)
{
    if(0 == iap_https_update_in_progress()) {
        cJSON* root = cJSON_Parse(data);
        if(nullptr == root) {
            ESP_LOGW(LOG_TAG_OTA, "Error parsing JSON OTA request.");
        } else {
            bool check = false;
            cJSON* checkItem = cJSON_GetObjectItem(root, "check");
            if(nullptr != checkItem) {
                check = (cJSON_IsTrue(checkItem)) ? true : false;
                if(check) {
                    // Check if there's a new firmware image available.
                    ESP_LOGI(LOG_TAG_OTA, "Requesting OTA firmware upgrade.");
                    iap_https_check_now();

                    char* reqAckTopic = (char*) calloc((topicLen + 1), sizeof(char));
                    const char* reqAckData = "";

                    if(nullptr == reqAckTopic) {
                        ESP_LOGE(LOG_TAG_OTA, "Couldn't allocate memory for request ack topic!");
                    } else {
                        strncpy(reqAckTopic, topic, topicLen);
                        if(MqttManager::ERR_OK != mqttMgr.publish(reqAckTopic, reqAckData, strlen(reqAckData), MqttManager::QOS_EXACTLY_ONCE, true)) {
                            ESP_LOGE(LOG_TAG_OTA, "Error publishing request ack.");
                        }
                    }
                } else {
                    ESP_LOGD(LOG_TAG_OTA, "Check request set to false.");
                }
            } else {
                // could have been our "ack", so just debug log it.
                ESP_LOGD(LOG_TAG_OTA, "No valid check request found.");
            }
            cJSON_Delete(root);
        }
    } else {
        ESP_LOGI(LOG_TAG_OTA, "OTA firmware upgrade already in progress. Dropping request.");
    }
}

void iapHttpsEventCallback(iap_https_event_t* event)
{
    iap_https_event_id_t eventId = event->event_id;

    ESP_LOGD(LOG_TAG_OTA, "IAP_HTTPS_EVENT received: %s (0x%08x)", IAP_HTTPS_EVENT_ID_TO_STR(eventId), eventId);

    switch(eventId) {
        case IAP_HTTPS_EVENT_CHECK_FOR_UPDATE:
            pwrMgr.setKeepAwakeForce(true); // signal to power manager that we need to stay awake
            break;

        case IAP_HTTPS_EVENT_UP_TO_DATE:
        case IAP_HTTPS_EVENT_UPGRADE_ERROR:
            pwrMgr.setKeepAwakeForce(false); // signal to power manager that we don't need to stay awake anymore
            break;

        case IAP_HTTPS_EVENT_UPGRADE_FINISHED:
            pwrMgr.setKeepAwakeForce(false); // signal to power manager that we don't need to stay awake anymore
            ESP_LOGI(LOG_TAG_OTA, "Upgrade finished successfully. Automatic re-boot in 2 seconds ...");
            vTaskDelay(2000 / portTICK_RATE_MS);
            pwrMgr.reboot();
            break;

        default:
            ESP_LOGW(LOG_TAG_OTA, "Unhandled IAP_HTTPS_EVENT received: %s (0x%08x)", IAP_HTTPS_EVENT_ID_TO_STR(eventId), eventId);
            break;
    }
}

// ********************************************************************
// SPIFFS init
// ********************************************************************
esp_err_t initializeSpiffs(void)
{
    esp_err_t ret = ESP_OK;

    esp_vfs_spiffs_conf_t conf = {
      .base_path = filepathConfigStore,
      .partition_label = partlabelConfigStore,
      .max_files = 4,
      .format_if_mount_failed = true
    };

    ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(LOG_TAG_SPIFFS, "Failed to mount or format config filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(LOG_TAG_SPIFFS, "Failed to find config partition");
        } else {
            ESP_LOGE(LOG_TAG_SPIFFS, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
    }

    if (ESP_OK == ret) {
        size_t total = 0, used = 0;
        ret = esp_spiffs_info(partlabelConfigStore, &total, &used);
        if (ret != ESP_OK) {
            ESP_LOGE(LOG_TAG_SPIFFS, "Failed to get partition information (%s)", esp_err_to_name(ret));
        } else {
            ESP_LOGI(LOG_TAG_SPIFFS, "Partition size: total: %d, used: %d", total, used);
        }
    }

    return ret;
}

// ********************************************************************
// settings manager helpers
// ********************************************************************
#define MQTT_CONFIG_TOPIC_PRE                    "whan/irrigation/"
#define MQTT_CONFIG_TOPIC_PRE_LEN                16
#define MQTT_CONFIG_IRRIG_TOPIC_POST_SET         "/irrig_config/set"
#define MQTT_CONFIG_IRRIG_TOPIC_POST_SET_LEN     17
#define MQTT_CONFIG_HARDWARE_TOPIC_POST_SET      "/hardware_config/set"
#define MQTT_CONFIG_HARDWARE_TOPIC_POST_SET_LEN  20
void mqttIrrigConfigSetCallback(const char* topic, int topicLen, const char* data, int dataLen);
void mqttHardwareConfigSetCallback(const char* topic, int topicLen, const char* data, int dataLen);

esp_err_t initializeSettingsMgr(void)
{
    esp_err_t ret = ESP_OK;

    settingsMgr.init();

    // try to read config files from SPIFFS
    settingsMgr.readIrrigationConfigFile();
    settingsMgr.readHardwareConfigFile();

    // subscribe to the config topics
    static char irrigTopic[MQTT_CONFIG_TOPIC_PRE_LEN + MQTT_CONFIG_IRRIG_TOPIC_POST_SET_LEN + 12 + 1];
    static char hardwareTopic[MQTT_CONFIG_TOPIC_PRE_LEN + MQTT_CONFIG_HARDWARE_TOPIC_POST_SET_LEN + 12 + 1];
    static uint8_t mac_addr[6];

    ret = esp_wifi_get_mac(ESP_IF_WIFI_STA, mac_addr);

    if(ESP_OK == ret) {
        // subscribe to the irrigation config topic
        memcpy(irrigTopic, MQTT_CONFIG_TOPIC_PRE, MQTT_CONFIG_TOPIC_PRE_LEN);
        for(int i=0; i<6; i++) {
            sprintf(&irrigTopic[MQTT_CONFIG_TOPIC_PRE_LEN + i*2], "%02x", mac_addr[i]);
        }

        memcpy(&irrigTopic[MQTT_CONFIG_TOPIC_PRE_LEN + 12], MQTT_CONFIG_IRRIG_TOPIC_POST_SET, MQTT_CONFIG_IRRIG_TOPIC_POST_SET_LEN);
        irrigTopic[MQTT_CONFIG_TOPIC_PRE_LEN + MQTT_CONFIG_IRRIG_TOPIC_POST_SET_LEN + 12] = 0;
        
        if(MqttManager::ERR_OK != mqttMgr.subscribe(irrigTopic, MqttManager::QOS_EXACTLY_ONCE, mqttIrrigConfigSetCallback)) {
            ESP_LOGW(LOG_TAG_MQTT_CFG_SETUP, "Failed to subscribe to irrigation config topic!");
            ret = -1;
        }

        // subscribe to the hardware config topic
        memcpy(hardwareTopic, MQTT_CONFIG_TOPIC_PRE, MQTT_CONFIG_TOPIC_PRE_LEN);
        for(int i=0; i<6; i++) {
            sprintf(&hardwareTopic[MQTT_CONFIG_TOPIC_PRE_LEN + i*2], "%02x", mac_addr[i]);
        }

        memcpy(&hardwareTopic[MQTT_CONFIG_TOPIC_PRE_LEN + 12], MQTT_CONFIG_HARDWARE_TOPIC_POST_SET, MQTT_CONFIG_HARDWARE_TOPIC_POST_SET_LEN);
        hardwareTopic[MQTT_CONFIG_TOPIC_PRE_LEN + MQTT_CONFIG_HARDWARE_TOPIC_POST_SET_LEN + 12] = 0;

        if(MqttManager::ERR_OK != mqttMgr.subscribe(hardwareTopic, MqttManager::QOS_EXACTLY_ONCE, mqttHardwareConfigSetCallback)) {
            ESP_LOGW(LOG_TAG_MQTT_CFG_SETUP, "Failed to subscribe to hardware config topic!");
            ret = -1;
        }
    } else {
        ESP_LOGW(LOG_TAG_MQTT_CFG_SETUP, "Failed to get WiFi MAC address for irrigation config topic subscription.");
        ret = -1;
    }

    return ret;
}

void mqttIrrigConfigSetCallback(const char* topic, int topicLen, const char* data, int dataLen)
{
    static char topicBuf[MQTT_CONFIG_TOPIC_PRE_LEN+MQTT_CONFIG_IRRIG_TOPIC_POST_SET_LEN+12+1];
    strncpy(topicBuf, topic, MIN(sizeof(topicBuf)-1, topicLen));
    topicBuf[MIN(sizeof(topicBuf), topicLen)] = 0;

    if (SettingsManager::ERR_OK == settingsMgr.updateIrrigationConfig(data, dataLen, false)) {
        // TBD: publish state somewhere
    }
    // clear the topic, so we won't parse it again
    mqttMgr.publish(topicBuf, nullptr, 0, MqttManager::QOS_EXACTLY_ONCE, true);
}

void mqttHardwareConfigSetCallback(const char* topic, int topicLen, const char* data, int dataLen)
{
    static char topicBuf[MQTT_CONFIG_TOPIC_PRE_LEN+MQTT_CONFIG_HARDWARE_TOPIC_POST_SET_LEN+12+1];
    strncpy(topicBuf, topic, MIN(sizeof(topicBuf)-1, topicLen));
    topicBuf[MIN(sizeof(topicBuf), topicLen)] = 0;

    if (SettingsManager::ERR_OK == settingsMgr.updateHardwareConfig(data, dataLen, false)) {
        // TBD: publish state somewhere
    }
    // clear the topic, so we won't parse it again
    mqttMgr.publish(topicBuf, nullptr, 0, MqttManager::QOS_EXACTLY_ONCE, true);
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
    // alternative: esp_log_set_vprintf(previousLogVprintf);

    #if defined(CONFIG_LOG_DEFAULT_LEVEL) && (CONFIG_LOG_DEFAULT_LEVEL > ESP_LOG_INFO)
    esp_log_level_set("phy_init", ESP_LOG_INFO);
    #endif
}

extern "C" void app_main()
{
    ESP_LOGI("main", "%s starting ...", VERSION_STRING);

    #if defined(CONFIG_LOG_DEFAULT_LEVEL) && (CONFIG_LOG_DEFAULT_LEVEL > ESP_LOG_INFO)
    ESP_LOGI("main", "Decreasing phy_init log level to INFO.");
    esp_log_level_set("phy_init", ESP_LOG_INFO);
    #endif

    ESP_ERROR_CHECK( nvs_flash_init() );

    // Initialize WiFi, but don't start yet.
    initializeWifi();

    // Initialize the SPIFFS, which may contain a config file
    ESP_ERROR_CHECK( initializeSpiffs() );

    // Initialize settings storage including setup of hooks, initial load from file, etc.
    ESP_ERROR_CHECK( initializeSettingsMgr() );

    // Prepare global mqtt clientName (needed due to lack of named initializers in C99)
    // and init the manager.
    ESP_ERROR_CHECK( initializeMqttMgr() );

    initializeOta();

    // Start WiFi. Events will start/stop MQTT client
    ESP_ERROR_CHECK( esp_wifi_start() );

    TimeSystem_Init();

    ConsoleInit(true, ConsoleStartHook, ConsoleExitHook);

    // Register config hooks for classes that have no init or task startup functions and therefore can't do it
    // on their own
    settingsMgr.registerHardwareConfigUpdatedHook(pwrMgr.hardwareConfigUpdatedHookDispatch, &pwrMgr);
    settingsMgr.registerIrrigConfigUpdatedHook(irrigPlanner.irrigConfigUpdatedHookDispatch, &irrigPlanner);

    // ... and initiate an initial settings update for them
    pwrMgr.hardwareConfigUpdated();
    irrigPlanner.irrigConfigUpdated();

    irrigCtrl.start();
}
