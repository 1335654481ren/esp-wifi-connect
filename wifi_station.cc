#include "wifi_station.h"
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <nvs.h>
#include "nvs_flash.h"
#include <esp_netif.h>
#include <esp_system.h>

#define TAG "wifi"
#define WIFI_EVENT_CONNECTED BIT0
#define WIFI_EVENT_FAILED BIT1
#define MAX_RECONNECT_COUNT 5

WifiStation& WifiStation::GetInstance() {
    static WifiStation instance;
    return instance;
}

WifiStation::WifiStation() {
    // Create the event group
    event_group_ = xEventGroupCreate();
    has_wifi_cfg_ = ReadConfig();
}

void WifiStation::SaveConfig(int num, bool status) {
    // Get ssid and password from NVS
    nvs_handle_t nvs_handle;
    auto ret = nvs_open("wifi", NVS_READWRITE, &nvs_handle);
    if (ret == ESP_OK) {
        std::string con_cnt = std::string("connect_cnt") + std::to_string(num);
        esp_err_t err = nvs_get_i32(nvs_handle, con_cnt.c_str(), &wifi_cfg_[num].connect_cnt);
        if (err == ESP_OK) {
            if (status == true) {
                if(wifi_cfg_[num].connect_cnt < 0) {
                    wifi_cfg_[num].connect_cnt = 0;
                }
                wifi_cfg_[num].connect_cnt++;
                ESP_LOGI(TAG,"Connect wifi config : ssid :%s ,passwd:%s ++", wifi_cfg_[num].cfg.sta.ssid, wifi_cfg_[num].cfg.sta.password);
            } else {
                ESP_LOGW(TAG,"Connect wifi config : ssid :%s ,passwd:%s  failed, ---", wifi_cfg_[num].cfg.sta.ssid, wifi_cfg_[num].cfg.sta.password);
                if (wifi_cfg_[num].connect_cnt > 0 ) {
                    wifi_cfg_[num].connect_cnt = 0;
                }
                wifi_cfg_[num].connect_cnt--;
                // connect fail 3 times to delete wifi record
                if (wifi_cfg_[num].connect_cnt < -3) {
                    ESP_LOGE(TAG,"Delete wifi config : ssid :%s ,passwd:%s", wifi_cfg_[num].cfg.sta.ssid, wifi_cfg_[num].cfg.sta.password);
                    std::string wifi_flag_key = std::string("wifi_flag") + std::to_string(num);
                    ESP_ERROR_CHECK(nvs_set_u8(nvs_handle, wifi_flag_key.c_str(), 0));
                    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
                    std::string ssid_key = std::string("ssid") + std::to_string(num);
                    std::string psw_key = std::string("psw") + std::to_string(num); 
                    ESP_ERROR_CHECK(nvs_erase_key(nvs_handle, ssid_key.c_str()));
                    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
                    ESP_ERROR_CHECK(nvs_erase_key(nvs_handle, psw_key.c_str()));
                    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
                }
            }
            ESP_ERROR_CHECK(nvs_set_i32(nvs_handle, con_cnt.c_str(), wifi_cfg_[num].connect_cnt));
            ESP_ERROR_CHECK(nvs_commit(nvs_handle));
        }
    }
    // Commit the changes
    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
    nvs_close(nvs_handle);
}

uint8_t WifiStation::ReadConfig() {
    uint8_t wifi_flag = 0;
    // Get ssid and password from NVS
    nvs_handle_t nvs_handle;
    auto ret = nvs_open("wifi", NVS_READWRITE, &nvs_handle);
    if (ret == ESP_OK) {
        for(int num = 0; num < WIFI_CFG_MAX; num++) {
            std::string wifi_flag_key = std::string("wifi_flag") + std::to_string(num);
            esp_err_t err = nvs_get_u8(nvs_handle, wifi_flag_key.c_str(), &wifi_cfg_[num].flag);
            //ESP_ERROR_CHECK(nvs_get_u8(nvs_handle, wifi_flag_key.c_str(), &wifi_cfg_[num].flag));
            if (err == ESP_ERR_NVS_NOT_FOUND) {
                ESP_LOGW(TAG, "err(%d)wifi_flag not found, setting default value. %s = %d", err, wifi_flag_key.c_str(), wifi_cfg_[num].flag);
                ESP_ERROR_CHECK(nvs_set_u8(nvs_handle, wifi_flag_key.c_str(), wifi_flag));
                ESP_ERROR_CHECK(nvs_commit(nvs_handle));
                std::string con_cnt = std::string("connect_cnt") + std::to_string(num);
                int32_t connect_cnt = 0;
                ESP_ERROR_CHECK(nvs_set_i32(nvs_handle, con_cnt.c_str(), connect_cnt));
                ESP_ERROR_CHECK(nvs_commit(nvs_handle));
            }
            ESP_LOGI(TAG, "Get : %s = %d", wifi_flag_key.c_str(), wifi_cfg_[num].flag);
            if(wifi_cfg_[num].flag == true) {
                wifi_flag = true;
                std::string ssid_key = std::string("ssid") + std::to_string(num);
                std::string psw_key = std::string("psw") + std::to_string(num);
                std::string con_cnt = std::string("connect_cnt") + std::to_string(num);
                ESP_ERROR_CHECK(nvs_get_i32(nvs_handle, con_cnt.c_str(), &wifi_cfg_[num].connect_cnt));
                size_t length = sizeof(wifi_cfg_[num].cfg.sta.ssid);
                ESP_ERROR_CHECK(nvs_get_str(nvs_handle, ssid_key.c_str(), (char*)wifi_cfg_[num].cfg.sta.ssid, &length));
                length = sizeof(wifi_cfg_[num].cfg.sta.password);
                ssid_ += std::string((char*)wifi_cfg_[num].cfg.sta.ssid); 
                ESP_ERROR_CHECK(nvs_get_str(nvs_handle, psw_key.c_str(), (char*)wifi_cfg_[num].cfg.sta.password, &length));
                ESP_LOGI(TAG,"Get wifi config : ssid: %s  psw: %s , connect_cnt: %ld", wifi_cfg_[num].cfg.sta.ssid, wifi_cfg_[num].cfg.sta.password,wifi_cfg_[num].connect_cnt);
            } else {
                wifi_cfg_[num].flag = false;
                wifi_cfg_[num].connect_cnt = 0;
            }
        }
        // Commit the changes
        ESP_ERROR_CHECK(nvs_commit(nvs_handle));
        nvs_close(nvs_handle);
    } else {
        ESP_LOGE(TAG, "Open wifi nvs flash Error");
    }
    return wifi_flag;
}

WifiStation::~WifiStation() {
    vEventGroupDelete(event_group_);
}

void WifiStation::SetAuth(const std::string &&ssid, const std::string &&password) {
    ssid_ = ssid;
    password_ = password;
}

void WifiStation::Start() {
    if (has_wifi_cfg_ == false){
        ESP_LOGW(TAG, "Have no wifi config ,start config wifi");
        return;
    }
    // Initialize the TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WifiStation::WifiEventHandler,
                                                        this,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &WifiStation::IpEventHandler,
                                                        this,
                                                        &instance_got_ip));

    // Create the default event loop
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif != NULL);
    // Initialize the WiFi stack in station mode
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // Start the WiFi stack
    ESP_ERROR_CHECK(esp_wifi_start());
    // Wait for the WiFi stack to start
    auto bits = xEventGroupWaitBits(event_group_, WIFI_EVENT_CONNECTED | WIFI_EVENT_FAILED, pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & WIFI_EVENT_FAILED) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        ESP_LOGE(TAG, "WifiStation failed");
        // Reset the WiFi stack
        ESP_ERROR_CHECK(esp_wifi_stop());
        ESP_ERROR_CHECK(esp_wifi_deinit());
        // 取消注册事件处理程序
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
        esp_netif_destroy(sta_netif);
        sta_netif = NULL;
        esp_netif_deinit();
        return;
    }
    SaveConfig(wifi_num_, true);
    ESP_LOGI(TAG, "Connected to %s rssi=%d channel=%d", ssid_.c_str(), GetRssi(), GetChannel());
}

int8_t WifiStation::GetRssi() {
    // Get station info
    wifi_ap_record_t ap_info;
    ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&ap_info));
    return ap_info.rssi;
}

uint8_t WifiStation::GetChannel() {
    // Get station info
    wifi_ap_record_t ap_info;
    ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&ap_info));
    return ap_info.primary;
}

bool WifiStation::IsConnected() {
    return xEventGroupGetBits(event_group_) & WIFI_EVENT_CONNECTED;
}

void WifiStation::SetPowerSaveMode(bool enabled) {
    ESP_ERROR_CHECK(esp_wifi_set_ps(enabled ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE));
}

// Static event handler functions
void WifiStation::WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto* this_ = static_cast<WifiStation*>(arg);
    if (event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WIFI event start and then start scan ap");
        ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, false));
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(this_->event_group_, WIFI_EVENT_CONNECTED);
        if (this_->reconnect_count_ < MAX_RECONNECT_COUNT) {
            esp_wifi_connect();
            this_->reconnect_count_++;
            ESP_LOGW(TAG, "Reconnecting WiFi (attempt %d)", this_->reconnect_count_);
        } else {
            this_->SaveConfig(this_->wifi_num_, false);
            xEventGroupSetBits(this_->event_group_, WIFI_EVENT_FAILED);
            ESP_LOGE(TAG, "WiFi connection failed");
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "Scan done");
        uint16_t ap_count = 0;
        this_->scan_try_count_++;
        bool has_match_wifi = false;
        esp_wifi_scan_get_ap_num(&ap_count);
        ESP_LOGI(TAG, "Get %d aviable ap points", ap_count);
        if (ap_count > 0) {
            wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
            if (ap_records == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for AP records");
                return;
            }
            esp_wifi_scan_get_ap_records(&ap_count, ap_records);
            for (int i = 0; i < ap_count; i++) {
                ESP_LOGI(TAG, "SSID: %s, RSSI: %d, Authmode: %d", 
                            ap_records[i].ssid, 
                            ap_records[i].rssi, 
                            ap_records[i].authmode);
            }
            for(int num = 0; num < WIFI_CFG_MAX; num++) {
                if (this_->wifi_cfg_[num].flag == true) {
                    for (int i = 0; i < ap_count; i++) {
                        ESP_LOGI(TAG, "Match SSID: %s, RSSI: %d, Authmode: %d", 
                                    ap_records[i].ssid, 
                                    ap_records[i].rssi, 
                                    ap_records[i].authmode);
                        if (strcmp((const char *)this_->wifi_cfg_[num].cfg.sta.ssid, (const char *)ap_records[i].ssid) == 0) {
                            free(ap_records);
                            has_match_wifi = true;
                            ap_records = nullptr;
                            this_->wifi_cfg_[num].cfg.sta.failure_retry_cnt = 5;
                            ESP_LOGI(TAG, "Start connect to SSID:%s , PSW:%s", this_->wifi_cfg_[num].cfg.sta.ssid, this_->wifi_cfg_[num].cfg.sta.password);
                            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &this_->wifi_cfg_[num].cfg));
                            esp_wifi_connect();
                            this_->wifi_num_ = num;
                            break;
                        }
                    }
                }
            }
            if(this_->scan_try_count_ == 3) {
                xEventGroupSetBits(this_->event_group_, WIFI_EVENT_FAILED);
                ESP_LOGE(TAG, "WiFi scan fail");
            }
            if(ap_records != nullptr)
                free(ap_records);
            if(has_match_wifi == false && this_->scan_try_count_ < 3) {
                ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, false));
            }
        } else {
            ESP_LOGW(TAG, "Start Scan again try");
            ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, false));
        }
    } 
}

void WifiStation::IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto* this_ = static_cast<WifiStation*>(arg);
    auto* event = static_cast<ip_event_got_ip_t*>(event_data);

    char ip_address[16];
    esp_ip4addr_ntoa(&event->ip_info.ip, ip_address, sizeof(ip_address));
    this_->ip_address_ = ip_address;
    ESP_LOGI(TAG, "Got IP: %s", this_->ip_address_.c_str());
    xEventGroupSetBits(this_->event_group_, WIFI_EVENT_CONNECTED);
}
