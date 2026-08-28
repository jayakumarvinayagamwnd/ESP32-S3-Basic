#pragma once

#include <cstdint>
#include <cstddef>

#include "esp_err.h"
#include "esp_event.h"

class WifiManager {
public:
    static WifiManager& getInstance();

    esp_err_t start();
    bool isConnected() const;
    std::uint32_t ipAddress() const;
    const char* ssid() const;

private:
    static void eventHandler(void* handler_arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data);
    void handleEvent(esp_event_base_t event_base, int32_t event_id, void* event_data);
    esp_err_t applyNetworkConfig(std::size_t index);
    bool hasConfiguredNetwork(std::size_t index) const;
    bool switchToAlternateNetwork();

    std::uint8_t retryCount_ = 0;
    bool isConnected_ = false;
    bool isStarted_ = false;
    std::uint32_t ipAddress_ = 0;
    std::size_t activeNetworkIndex_ = 0;
    char activeSsid_[33] = {};
};
