#include <cstdio>
#include <string>
#include "esp_log.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "WifiManager.hpp"
#include "webserver.hpp"
namespace
{
    constexpr char kLogTag[] = "ESP32 S3-mini";
    WifiManager& sWifiManager = WifiManager::getInstance();
    WebServer& sWebServer = WebServer::getInstance();
}
extern "C" void app_main(void) {
    ESP_ERROR_CHECK(sWifiManager.start());
    ESP_ERROR_CHECK(sWebServer.start());

    std::uint32_t lastReportedIp = 0;
    while (true) {
        if (sWifiManager.isConnected() && sWebServer.isRunning()) {
            const std::uint32_t ip = sWifiManager.ipAddress();
            if (ip != lastReportedIp) {
                ESP_LOGI(kLogTag, "ESP32 S3-mini available at http://%u.%u.%u.%u/",
                         static_cast<unsigned>((ip >> 0) & 0xFF),
                         static_cast<unsigned>((ip >> 8) & 0xFF),
                         static_cast<unsigned>((ip >> 16) & 0xFF),
                         static_cast<unsigned>((ip >> 24) & 0xFF));
                lastReportedIp = ip;
            }
        }
        ESP_LOGI(kLogTag, "Application is running; Wi-Fi: %s; web server: %s",
            sWifiManager.isConnected() ? "connected" : "connecting",
            sWebServer.isRunning() ? "running" : "stopped");

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

}
