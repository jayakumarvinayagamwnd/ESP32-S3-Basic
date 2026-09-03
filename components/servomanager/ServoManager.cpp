#include "ServoManager.hpp"

#include <cmath>

#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr char kLogTag[] = "servo_manager";

constexpr i2c_port_t kI2cPort = I2C_NUM_0;
constexpr gpio_num_t kI2cSdaPin = GPIO_NUM_8;
constexpr gpio_num_t kI2cSclPin = GPIO_NUM_9;
constexpr std::uint32_t kI2cFreqHz = 400000;

constexpr std::uint8_t kPca9685Addr = 0x40;
constexpr std::uint8_t kRegMode1 = 0x00;
constexpr std::uint8_t kRegMode2 = 0x01;
constexpr std::uint8_t kRegPrescale = 0xFE;
constexpr std::uint8_t kRegLed0OnL = 0x06;

constexpr std::uint8_t kMode1Sleep = 0x10;
constexpr std::uint8_t kMode1AutoIncrement = 0x20;
constexpr std::uint8_t kMode2TotemPole = 0x04;

constexpr std::uint32_t kPcaOscillatorHz = 25000000;
constexpr std::uint16_t kPwmSteps = 4096;
constexpr std::uint16_t kServoPeriodUs = 20000;
constexpr std::uint16_t kMinPulseUs = 500;
constexpr std::uint16_t kMaxPulseUs = 2500;
constexpr std::uint16_t kMaxAngleDeg = 180;
constexpr std::uint16_t kPwmFreqHz = 50;

std::uint8_t computePrescale(std::uint16_t pwmFreqHz)
{
    const float prescaleValue = (static_cast<float>(kPcaOscillatorHz) /
                                 (static_cast<float>(kPwmSteps) * pwmFreqHz)) -
                                1.0f;
    return static_cast<std::uint8_t>(std::round(prescaleValue));
}

std::uint16_t angleToPulseWidthUs(std::uint16_t angleDeg)
{
    if (angleDeg > kMaxAngleDeg) {
        angleDeg = kMaxAngleDeg;
    }

    return static_cast<std::uint16_t>(
        kMinPulseUs + ((kMaxPulseUs - kMinPulseUs) * angleDeg) / kMaxAngleDeg);
}

std::uint16_t pulseWidthToOffCount(std::uint16_t pulseWidthUs)
{
    const std::uint32_t offCount = (static_cast<std::uint32_t>(pulseWidthUs) * kPwmSteps) /
                                   kServoPeriodUs;
    return static_cast<std::uint16_t>(offCount > (kPwmSteps - 1) ? (kPwmSteps - 1) : offCount);
}
}  // namespace

ServoManager& ServoManager::getInstance()
{
    static ServoManager instance;
    return instance;
}

esp_err_t ServoManager::start()
{
    if (isStarted_) {
        return ESP_OK;
    }

    i2c_config_t i2cConfig = {};
    i2cConfig.mode = I2C_MODE_MASTER;
    i2cConfig.sda_io_num = kI2cSdaPin;
    i2cConfig.scl_io_num = kI2cSclPin;
    i2cConfig.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2cConfig.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2cConfig.master.clk_speed = kI2cFreqHz;

    esp_err_t err = i2c_param_config(kI2cPort, &i2cConfig);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_driver_install(kI2cPort, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = writeRegister(kRegMode1, kMode1Sleep);
    if (err != ESP_OK) {
        return err;
    }

    err = writeRegister(kRegPrescale, computePrescale(kPwmFreqHz));
    if (err != ESP_OK) {
        return err;
    }

    err = writeRegister(kRegMode1, kMode1AutoIncrement);
    if (err != ESP_OK) {
        return err;
    }

    err = writeRegister(kRegMode2, kMode2TotemPole);
    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(5));

    isStarted_ = true;
    ESP_LOGI(kLogTag, "PCA9685 initialized at 50Hz on I2C0 (SDA=%d, SCL=%d)",
             static_cast<int>(kI2cSdaPin),
             static_cast<int>(kI2cSclPin));
    ESP_LOGI(kLogTag,
             "Power sanity: VCC=3.3V logic, V+=external 5V servo rail, and all grounds must be common");
    ESP_LOGI(kLogTag, "Servo output routes: Servo 1..4 -> PCA9685 channels 0..3");

    return ESP_OK;
}

esp_err_t ServoManager::setAngle(std::uint8_t channel, std::uint16_t angleDeg)
{
    if (channel > 15) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!isStarted_) {
        const esp_err_t err = start();
        if (err != ESP_OK) {
            return err;
        }
    }

    const std::uint16_t pulseWidthUs = angleToPulseWidthUs(angleDeg);
    const std::uint16_t offCount = pulseWidthToOffCount(pulseWidthUs);

    return setPwm(channel, 0, offCount);
}

esp_err_t ServoManager::writeRegister(std::uint8_t reg, std::uint8_t value)
{
    const std::uint8_t payload[2] = {reg, value};
    return i2c_master_write_to_device(kI2cPort, kPca9685Addr, payload, sizeof(payload),
                                      pdMS_TO_TICKS(100));
}

esp_err_t ServoManager::setPwm(std::uint8_t channel, std::uint16_t onCount, std::uint16_t offCount)
{
    const std::uint8_t reg = static_cast<std::uint8_t>(kRegLed0OnL + (4 * channel));
    const std::uint8_t payload[5] = {
        reg,
        static_cast<std::uint8_t>(onCount & 0xFF),
        static_cast<std::uint8_t>((onCount >> 8) & 0x0F),
        static_cast<std::uint8_t>(offCount & 0xFF),
        static_cast<std::uint8_t>((offCount >> 8) & 0x0F),
    };

    return i2c_master_write_to_device(kI2cPort, kPca9685Addr, payload, sizeof(payload),
                                      pdMS_TO_TICKS(100));
}
