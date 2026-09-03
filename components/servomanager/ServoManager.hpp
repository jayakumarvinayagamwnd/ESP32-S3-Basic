#pragma once

#include <cstdint>

#include "esp_err.h"

class ServoManager {
public:
    static ServoManager& getInstance();

    esp_err_t start();
    esp_err_t setAngle(std::uint8_t channel, std::uint16_t angleDeg);

private:
    esp_err_t writeRegister(std::uint8_t reg, std::uint8_t value);
    esp_err_t setPwm(std::uint8_t channel, std::uint16_t onCount, std::uint16_t offCount);

    bool isStarted_ = false;
};
