#include <cstdio>
#include <string>
#include "esp_log.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "led.hpp"
#include "WifiManager.hpp"
namespace
{
    constexpr char kLogTag[] = "bike_dash_cam";
    WifiManager& sWifiManager = WifiManager::getInstance();
}
extern "C" void app_main(void) {
    ledmanager::Led led;   
    ESP_ERROR_CHECK(sWifiManager.start());
    while (true) {
        led.on(0, 0, 20); // green: ON
        vTaskDelay(pdMS_TO_TICKS(500));
        led.off();
        vTaskDelay(pdMS_TO_TICKS(500));        
         if (sWifiManager.isConnected()) {
            const std::uint32_t ip = sWifiManager.ipAddress();
            ESP_LOGI(kLogTag, "Dashboard available at http://%u.%u.%u.%u/",
                     static_cast<unsigned>((ip >> 0) & 0xFF),
                     static_cast<unsigned>((ip >> 8) & 0xFF),
                     static_cast<unsigned>((ip >> 16) & 0xFF),
                     static_cast<unsigned>((ip >> 24) & 0xFF));
        }
    }

}
