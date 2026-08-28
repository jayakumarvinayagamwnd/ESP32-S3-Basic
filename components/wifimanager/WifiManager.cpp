#include "wifimanager.hpp"

#include <cstring>

#include <array>

#include "config.hpp"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

namespace {
constexpr char kLogTag[] = "wifi_manager";

struct Credentials {
    const char* ssid;
    const char* password;
};

constexpr std::array<Credentials, 2> kCredentials = {{
    {config::WIFI_SSID, config::WIFI_PASS},
    {config::WIFI_SSID_2, config::WIFI_PASS_2},
}};
}

WifiManager& WifiManager::getInstance()
{
    static WifiManager instance;
    return instance;
}

esp_err_t WifiManager::start()
{
    if (isStarted_) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&init_config)) != ESP_OK) {
        return err;
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiManager::eventHandler, this, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiManager::eventHandler, this, nullptr));

    ESP_ERROR_CHECK(esp_wifi_set_mode(config::WIFI_MODE));
    activeNetworkIndex_ = 0;
    if ((err = applyNetworkConfig(activeNetworkIndex_)) != ESP_OK) {
        return err;
    }
    ESP_ERROR_CHECK(esp_wifi_start());

    isStarted_ = true;
    ESP_LOGI(kLogTag, "Wi-Fi started in station mode");
    return ESP_OK;
}

bool WifiManager::isConnected() const
{
    return isConnected_;
}

std::uint32_t WifiManager::ipAddress() const
{
    return ipAddress_;
}

const char* WifiManager::ssid() const
{
    return activeSsid_;
}

void WifiManager::eventHandler(void* handler_arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    static_cast<WifiManager*>(handler_arg)->handleEvent(event_base, event_id, event_data);
}

void WifiManager::handleEvent(esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_ERROR_CHECK(esp_wifi_connect());
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        isConnected_ = false;
        if (config::AUTO_RECONNECT && retryCount_ < config::MAX_RETRY) {
            ++retryCount_;
            ESP_LOGW(kLogTag, "Wi-Fi disconnected from %s; reconnecting (%u/%u)", activeSsid_,
                     retryCount_, config::MAX_RETRY);
            ESP_ERROR_CHECK(esp_wifi_connect());
        } else if (config::AUTO_RECONNECT && switchToAlternateNetwork()) {
            retryCount_ = 0;
            ESP_LOGW(kLogTag, "Switching Wi-Fi network to %s", activeSsid_);
            ESP_ERROR_CHECK(esp_wifi_connect());
        } else {
            ESP_LOGE(kLogTag, "Wi-Fi connection failed for %s", activeSsid_);
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const auto* event = static_cast<const ip_event_got_ip_t*>(event_data);
        ipAddress_ = event->ip_info.ip.addr;
        retryCount_ = 0;
        isConnected_ = true;
        ESP_LOGI(kLogTag, "Wi-Fi connected to %s", activeSsid_);
    }
}

esp_err_t WifiManager::applyNetworkConfig(std::size_t index)
{
    if (index >= kCredentials.size() || !hasConfiguredNetwork(index)) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t wifi_config{};
    std::strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), kCredentials[index].ssid,
                 sizeof(wifi_config.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(wifi_config.sta.password), kCredentials[index].password,
                 sizeof(wifi_config.sta.password) - 1);
    const esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        return err;
    }

    activeNetworkIndex_ = index;
    std::strncpy(activeSsid_, kCredentials[index].ssid, sizeof(activeSsid_) - 1);
    activeSsid_[sizeof(activeSsid_) - 1] = '\0';
    return ESP_OK;
}

bool WifiManager::hasConfiguredNetwork(std::size_t index) const
{
    if (index >= kCredentials.size() || kCredentials[index].ssid == nullptr) {
        return false;
    }
    return kCredentials[index].ssid[0] != '\0';
}

bool WifiManager::switchToAlternateNetwork()
{
    for (std::size_t index = 0; index < kCredentials.size(); ++index) {
        if (index == activeNetworkIndex_ || !hasConfiguredNetwork(index)) {
            continue;
        }
        return applyNetworkConfig(index) == ESP_OK;
    }

    for (std::size_t index = 0; index < kCredentials.size(); ++index) {
        if (hasConfiguredNetwork(index)) {
            return applyNetworkConfig(index) == ESP_OK;
        }
    }

    return false;
}
