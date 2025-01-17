#ifndef _WIFI_SMARTCONFIG_H_
#define _WIFI_SMARTCONFIG_H_

#include <string>
#include "esp_event.h"

class WifiSmartConfiguration {
public:
    static WifiSmartConfiguration& GetInstance();
    void Start();
    // Delete copy constructor and assignment operator
    WifiSmartConfiguration(const WifiSmartConfiguration&) = delete;
    WifiSmartConfiguration& operator=(const WifiSmartConfiguration&) = delete;

private:
    // Private constructor
    WifiSmartConfiguration();
    ~WifiSmartConfiguration();
    std::string ssid_;
    std::string password_;
    int reconnect_count_;
    EventGroupHandle_t event_group_;
    void Save(const std::string &ssid, const std::string &password);

    // Event handlers
    static void WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
};

#endif // _WIFI_CONFIGURATION_AP_H_
