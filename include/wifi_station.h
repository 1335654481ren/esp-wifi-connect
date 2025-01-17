#ifndef _WIFI_STATION_H_
#define _WIFI_STATION_H_

#include <string>
#include <esp_wifi.h>
#include "esp_event.h"

#define WIFI_CFG_MAX 3

struct wifi_cfg{
    uint8_t flag;
    int32_t connect_cnt;
    wifi_config_t cfg;
};

class WifiStation {
public:
    static WifiStation& GetInstance();
    void SetAuth(const std::string &&ssid, const std::string &&password);
    void Start();
    bool IsConnected();
    int8_t GetRssi();
    std::string GetSsid() const { return ssid_; }
    std::string GetIpAddress() const { return ip_address_; }
    uint8_t GetChannel();
    void SaveConfig(int num, bool status);
    uint8_t ReadConfig();
    void SetPowerSaveMode(bool enabled);

private:
    WifiStation();
    ~WifiStation();
    WifiStation(const WifiStation&) = delete;
    WifiStation& operator=(const WifiStation&) = delete;

    EventGroupHandle_t event_group_;
    std::string ssid_;
    std::string password_;
    std::string ip_address_;
    int reconnect_count_ = 0;
    int scan_try_count_ = 0;
    int wifi_num_ = 0;
    bool has_wifi_cfg_ = false;
    wifi_cfg wifi_cfg_[WIFI_CFG_MAX];
    static void WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
};

#endif // _WIFI_STATION_H_
