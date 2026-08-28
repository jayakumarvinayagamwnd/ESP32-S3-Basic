#pragma once

#include <cstdint>

#include "esp_wifi.h"

namespace config {

inline constexpr char WIFI_SSID[] = "Jay";
inline constexpr char WIFI_PASS[] = "Jayant@54321";
inline constexpr char WIFI_SSID_2[] = "Pixel_Jay";
inline constexpr char WIFI_PASS_2[] = "LordSun#123456";

inline constexpr std::uint8_t MAX_RETRY = 5;
inline constexpr bool AUTO_RECONNECT = true;
inline constexpr wifi_mode_t WIFI_MODE = WIFI_MODE_STA;
}