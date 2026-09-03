#pragma once

#include <array>
#include <cstdint>

#include "../ledmanager/led.hpp"
#include "../servomanager/ServoManager.hpp"
#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

class WebServer {
public:
    static WebServer& getInstance();

    esp_err_t start();
    bool isRunning() const;

private:
    static void waitAndStartTask(void* arg);
    static void ledOffTimerCallback(TimerHandle_t timer);
    static esp_err_t rootGetHandler(httpd_req_t* req);
    static esp_err_t statusGetHandler(httpd_req_t* req);
    static esp_err_t ledGetHandler(httpd_req_t* req);
    static esp_err_t servoGetHandler(httpd_req_t* req);
    static esp_err_t servoSweepGetHandler(httpd_req_t* req);

    esp_err_t startHttpServer();
    esp_err_t setLedColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                          const char* colorName, bool enableAutoOff);
    esp_err_t setServoAngle(std::uint8_t servoIndex, std::uint16_t angleDeg);
    esp_err_t runServoSelfTest(std::uint8_t servoIndex);
    static void registerGetHandler(httpd_handle_t server, const char* uri,
                                   esp_err_t (*handler)(httpd_req_t*));

    bool isStarted_ = false;
    httpd_handle_t server_ = nullptr;
    ledmanager::Led led_;
    ServoManager servoManager_;
    TimerHandle_t ledOffTimer_ = nullptr;
    SemaphoreHandle_t ledMutex_ = nullptr;
    SemaphoreHandle_t servoMutex_ = nullptr;
    char ledColor_[16] = "off";
    std::array<std::uint16_t, 4> servoAnglesDeg_ = {0, 0, 0, 0};
};
