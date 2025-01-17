#include "wifi_smartconfig.h"
#include <cstdio>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_err.h>
#include "esp_smartconfig.h"
#include <esp_event.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <lwip/ip_addr.h>
#include <nvs.h>
#include <nvs_flash.h>

#define TAG "WifiSmartConfiguration"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define ESPTOUCH_DONE_BIT  BIT2
#define MAX_RECONNECT_COUNT 5

WifiSmartConfiguration& WifiSmartConfiguration::GetInstance() {
    static WifiSmartConfiguration instance;
    return instance;
}

WifiSmartConfiguration::WifiSmartConfiguration()
{
    event_group_ = xEventGroupCreate();
}

WifiSmartConfiguration::~WifiSmartConfiguration()
{
    if (event_group_) {
        vEventGroupDelete(event_group_);
    }
    // Unregister event handlers if they were registered
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiSmartConfiguration::WifiEventHandler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiSmartConfiguration::WifiEventHandler);
}

void WifiSmartConfiguration::Start()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_delete_default());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    // Register event handlers
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_t instance_got_sc;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WifiSmartConfiguration::WifiEventHandler,
                                                        this,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &WifiSmartConfiguration::WifiEventHandler,
                                                        this,
                                                        &instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(SC_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WifiSmartConfiguration::WifiEventHandler,
                                                        this,
                                                        &instance_got_sc));                                                    

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi esp_wifi_start");

    EventBits_t uxBits;
    while (true) {
        uxBits = xEventGroupWaitBits(event_group_, WIFI_CONNECTED_BIT | ESPTOUCH_DONE_BIT | WIFI_FAIL_BIT, true, false, portMAX_DELAY);
        if(uxBits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to ap");
            Save(ssid_, password_);
        }
        if(uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG, "smartconfig over");
            esp_smartconfig_stop();
        }
        if (uxBits & WIFI_FAIL_BIT) {
            ESP_LOGE(TAG, "WifiStation failed");
            ESP_LOGI(TAG, "Restarting the ESP32 in 3 second");
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_restart();
        }
    }
}


void WifiSmartConfiguration::Save(const std::string &ssid, const std::string &password)
{
    // Open the NVS flash
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open("wifi", NVS_READWRITE, &nvs_handle));
    // Write the SSID and password to the NVS flash
    uint8_t wifi_flag = 0;
    int32_t connect_cnt = 65535;
    int re_num = 0;
    for (int num = 0; num < 3; num++) {
        std::string wifi_flag_key = std::string("wifi_flag") + std::to_string(num);
        nvs_get_u8(nvs_handle, wifi_flag_key.c_str(), &wifi_flag);
        if (wifi_flag == true) {
            int32_t connect_cnt_temp = 0;
            std::string con_cnt = std::string("connect_cnt") + std::to_string(num);
            ESP_ERROR_CHECK(nvs_get_i32(nvs_handle, con_cnt.c_str(), &connect_cnt_temp));
            if (connect_cnt_temp <= connect_cnt) {
                re_num = num;
                connect_cnt = connect_cnt_temp;
            }
            std::string ssid_key = std::string("ssid") + std::to_string(num);
            size_t length = 32;
            char ssid_str[length];
            ESP_ERROR_CHECK(nvs_get_str(nvs_handle, ssid_key.c_str(), ssid_str, &length));
            if (strcmp(ssid_str, ssid.c_str()) == 0) {
                re_num = num;
                break;
            }           
        } else {
            re_num = num;
            break;
        }
    }
    std::string wifi_flag_key = std::string("wifi_flag") + std::to_string(re_num);
    std::string ssid_key = std::string("ssid") + std::to_string(re_num);
    std::string psw_key = std::string("psw") + std::to_string(re_num);

    ESP_ERROR_CHECK(nvs_set_u8(nvs_handle, wifi_flag_key.c_str(), true));
    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
    ESP_ERROR_CHECK(nvs_set_str(nvs_handle, ssid_key.c_str(), ssid.c_str()));
    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
    ESP_ERROR_CHECK(nvs_set_str(nvs_handle, psw_key.c_str(), password.c_str()));
    // Commit the changes
    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
    // Close the NVS flash
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "WiFi configuration saved");
    // Use xTaskCreate to create a new task that restarts the ESP32
    xTaskCreate([](void *ctx) {
        ESP_LOGI(TAG, "Restarting the ESP32 in 3 second");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }, "restart_task", 4096, NULL, 5, NULL);
}

void WifiSmartConfiguration::WifiEventHandler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    // ESP_LOGW(TAG, "WifiEventHandler: %s = %d", event_base, (int)event_id);
    WifiSmartConfiguration* self = static_cast<WifiSmartConfiguration*>(arg);
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGW(TAG, "WiFi Start smartconfig ..... ");
        ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_AIRKISS) );
        smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
        ESP_ERROR_CHECK( esp_smartconfig_start(&cfg) );
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(self->event_group_, WIFI_CONNECTED_BIT);
        if (self->reconnect_count_ < MAX_RECONNECT_COUNT) {
            esp_wifi_connect();
            self->reconnect_count_++;
            ESP_LOGI(TAG, "Reconnecting WiFi (attempt %d)", self->reconnect_count_);
        } else {
            xEventGroupSetBits(self->event_group_, WIFI_FAIL_BIT);
            ESP_LOGI(TAG, "WiFi connection failed");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(self->event_group_, WIFI_CONNECTED_BIT);
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "Scan done");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG, "Found channel");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        ESP_LOGI(TAG, "Got SSID and password");
        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        char ssid[32] = { 0 };
        char password[64] = { 0 };
        uint8_t rvd_data[33] = { 0 };
        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.failure_retry_cnt = 5;
#ifdef CONFIG_SET_MAC_ADDRESS_OF_TARGET_AP
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true) {
            ESP_LOGI(TAG, "Set MAC address of target AP: "MACSTR" ", MAC2STR(evt->bssid));
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }
#endif

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(TAG, "SSID:%s", ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", password);
        self->ssid_ = std::string(ssid);
        self->password_ = std::string(password);

        ESP_ERROR_CHECK( esp_wifi_disconnect() );
        ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
        esp_wifi_connect();
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        ESP_LOGI(TAG, "SC_EVENT_SEND_ACK_DONE");
        xEventGroupSetBits(self->event_group_, ESPTOUCH_DONE_BIT);
    }
}

