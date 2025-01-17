#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include "system_info.h"

#include <wifi_station.h>
#include <wifi_configuration_ap.h>
#include <wifi_smartconfig.h>

#define TAG "main"

extern "C" void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_VERBOSE);
    // Initialize the default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_LOGI(TAG, "App start");
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_ERROR_CHECK(ret);
   // Try to connect to WiFi, if failed, launch the WiFi configuration AP
    auto& wifi_station = WifiStation::GetInstance();
    wifi_station.Start();
    if (!wifi_station.IsConnected()) {
        std::string hint;
        if(wifi_cfg_type == SOFT_AP_WEB) {
            auto& wifi_ap = WifiConfigurationAp::GetInstance();
            wifi_ap.SetSsidPrefix("Xiaozhi");
            // 显示 WiFi 配置 AP 的 SSID 和 Web 服务器 URL
            hint = "请在手机上连接热点 ";
            hint += wifi_ap.GetSsid();
            hint += "，然后打开浏览器访问 ";
            hint += wifi_ap.GetWebServerUrl();
            ESP_LOGI(TAG,"%s",hint.c_str());
            wifi_ap.Start();
        } else {
            auto& wifi_ap = WifiSmartConfiguration::GetInstance();
            // 显示 WiFi 配置 AP 的 SSID 和 Web 服务器 URL
            hint = "请关注微信小程序:(AI智能硬件)配网（“。”）";
            ESP_LOGI(TAG,"%s",hint.c_str());
            display->SetStatus(hint);
            wifi_ap.Start();
        }
    }
    // Dump CPU usage every 10 second
    while (true) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        // SystemInfo::PrintRealTimeStats(pdMS_TO_TICKS(1000));
        int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        int min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "Free internal: %u minimal internal: %u", free_sram, min_free_sram);
    }
}
