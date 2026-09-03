#include "webserver.hpp"

#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "WifiManager.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "index_html.hpp"

namespace {
constexpr char kLogTag[] = "webserver";
constexpr std::uint32_t kLedAutoOffMs = 60000;
constexpr std::uint8_t kServoCount = 4;
constexpr std::uint8_t kServoMinId = 1;
constexpr std::uint8_t kServoMaxId = 4;
constexpr std::uint16_t kServoMinAngle = 0;
constexpr std::uint16_t kServoMaxAngle = 180;
constexpr TickType_t kServoSweepStepDelay = pdMS_TO_TICKS(180);

WebServer* gWebServerInstance = nullptr;
}

WebServer& WebServer::getInstance()
{
    static WebServer instance;
    return instance;
}

esp_err_t WebServer::start()
{
    if (isStarted_) {
        return ESP_ERR_INVALID_STATE;
    }

    if (ledMutex_ == nullptr) {
        ledMutex_ = xSemaphoreCreateMutex();
        if (ledMutex_ == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (servoMutex_ == nullptr) {
        servoMutex_ = xSemaphoreCreateMutex();
        if (servoMutex_ == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t err = servoManager_.start();
    if (err != ESP_OK) {
        return err;
    }
    for (std::uint8_t servoIndex = 0; servoIndex < kServoCount; ++servoIndex) {
        err = setServoAngle(servoIndex, servoAnglesDeg_[servoIndex]);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (ledOffTimer_ == nullptr) {
        ledOffTimer_ = xTimerCreate("led_off", pdMS_TO_TICKS(kLedAutoOffMs), pdFALSE, this,
                                    &WebServer::ledOffTimerCallback);
        if (ledOffTimer_ == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    gWebServerInstance = this;

    isStarted_ = true;
    const BaseType_t created = xTaskCreate(waitAndStartTask, "webserver", 4096, this, 5, nullptr);
    if (created != pdPASS) {
        isStarted_ = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(kLogTag, "Waiting for Wi-Fi before starting HTTP server");
    return ESP_OK;
}

bool WebServer::isRunning() const
{
    return server_ != nullptr;
}

void WebServer::waitAndStartTask(void* arg)
{
    auto* self = static_cast<WebServer*>(arg);
    WifiManager& wifi = WifiManager::getInstance();

    while (!wifi.isConnected()) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    const esp_err_t err = self->startHttpServer();
    if (err != ESP_OK) {
        ESP_LOGE(kLogTag, "Failed to start HTTP server: %s", esp_err_to_name(err));
        self->isStarted_ = false;
    }

    vTaskDelete(nullptr);
}

void WebServer::registerGetHandler(httpd_handle_t server, const char* uri,
                                   esp_err_t (*handler)(httpd_req_t*))
{
    httpd_uri_t route = {};
    route.uri = uri;
    route.method = HTTP_GET;
    route.handler = handler;
    route.user_ctx = nullptr;
    httpd_register_uri_handler(server, &route);
}

void WebServer::ledOffTimerCallback(TimerHandle_t timer)
{
    auto* self = static_cast<WebServer*>(pvTimerGetTimerID(timer));
    if (self == nullptr) {
        return;
    }

    const esp_err_t err = self->setLedColor(0, 0, 0, "off", false);
    if (err == ESP_OK) {
        ESP_LOGI(kLogTag, "LED auto-off applied after timeout");
    }
}

esp_err_t WebServer::startHttpServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.lru_purge_enable = true;

    const esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK) {
        return err;
    }

    registerGetHandler(server_, "/", rootGetHandler);
    registerGetHandler(server_, "/api/status", statusGetHandler);
    registerGetHandler(server_, "/api/led", ledGetHandler);
    registerGetHandler(server_, "/api/servo", servoGetHandler);
    registerGetHandler(server_, "/api/servo/sweep", servoSweepGetHandler);

    ESP_LOGI(kLogTag, "HTTP server started on port %d", config.server_port);
    return ESP_OK;
}

esp_err_t WebServer::rootGetHandler(httpd_req_t* req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebServer::statusGetHandler(httpd_req_t* req)
{
    WifiManager& wifi = WifiManager::getInstance();
    char body[192];
    const char* ssid = wifi.ssid();
    if (ssid == nullptr || ssid[0] == '\0') {
        ssid = "";
    }

    if (wifi.isConnected()) {
        const std::uint32_t ip = wifi.ipAddress();
        std::snprintf(body, sizeof(body),
                      "{\"connected\":true,\"ssid\":\"%s\",\"ip\":\"%u.%u.%u.%u\"}",
                      ssid,
                      static_cast<unsigned>((ip >> 0) & 0xFF),
                      static_cast<unsigned>((ip >> 8) & 0xFF),
                      static_cast<unsigned>((ip >> 16) & 0xFF),
                      static_cast<unsigned>((ip >> 24) & 0xFF));
    } else {
        std::snprintf(body, sizeof(body),
                      "{\"connected\":false,\"ssid\":\"%s\",\"ip\":\"\"}", ssid);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebServer::setLedColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                                 const char* colorName, bool enableAutoOff)
{
    if (ledMutex_ == nullptr || ledOffTimer_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(ledMutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (red == 0 && green == 0 && blue == 0) {
        led_.off();
    } else {
        led_.on(red, green, blue);
    }

    std::strncpy(ledColor_, colorName, sizeof(ledColor_) - 1);
    ledColor_[sizeof(ledColor_) - 1] = '\0';

    xSemaphoreGive(ledMutex_);

    if (enableAutoOff) {
        xTimerStop(ledOffTimer_, 0);
        xTimerReset(ledOffTimer_, 0);
    } else {
        xTimerStop(ledOffTimer_, 0);
    }

    return ESP_OK;
}

esp_err_t WebServer::setServoAngle(std::uint8_t servoIndex, std::uint16_t angleDeg)
{
    if (servoMutex_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (servoIndex >= kServoCount || angleDeg > kServoMaxAngle) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(servoMutex_, pdMS_TO_TICKS(150)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const esp_err_t err = servoManager_.setAngle(servoIndex, angleDeg);
    if (err == ESP_OK) {
        servoAnglesDeg_[servoIndex] = angleDeg;
    }

    xSemaphoreGive(servoMutex_);
    return err;
}

esp_err_t WebServer::runServoSelfTest(std::uint8_t servoIndex)
{
    const std::uint16_t testAngles[] = {0, 45, 0};
    if (servoIndex >= kServoCount) {
        return ESP_ERR_INVALID_ARG;
    }

    for (std::size_t index = 0; index < (sizeof(testAngles) / sizeof(testAngles[0])); ++index) {
        const esp_err_t err = setServoAngle(servoIndex, testAngles[index]);
        if (err != ESP_OK) {
            return err;
        }
        vTaskDelay(kServoSweepStepDelay);
    }

    return ESP_OK;
}

esp_err_t WebServer::ledGetHandler(httpd_req_t* req)
{
    if (gWebServerInstance == nullptr) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"server unavailable\"}");
    }

    char query[64] = {};
    char color[16] = {};
    const std::size_t queryLen = httpd_req_get_url_query_len(req);
    if (queryLen == 0 || queryLen >= sizeof(query) ||
        httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "color", color, sizeof(color)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"missing color\"}");
    }

    esp_err_t err = ESP_ERR_INVALID_ARG;
    bool enableAutoOff = true;
    const char* resolvedColor = "off";

    if (std::strcmp(color, "red") == 0) {
        resolvedColor = "red";
        err = gWebServerInstance->setLedColor(20, 0, 0, resolvedColor, true);
    } else if (std::strcmp(color, "green") == 0) {
        resolvedColor = "green";
        err = gWebServerInstance->setLedColor(0, 20, 0, resolvedColor, true);
    } else if (std::strcmp(color, "blue") == 0) {
        resolvedColor = "blue";
        err = gWebServerInstance->setLedColor(0, 0, 20, resolvedColor, true);
    } else if (std::strcmp(color, "off") == 0) {
        resolvedColor = "off";
        enableAutoOff = false;
        err = gWebServerInstance->setLedColor(0, 0, 0, resolvedColor, false);
    }

    if (err != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"invalid color\"}");
    }

    char body[96];
    std::snprintf(body, sizeof(body),
                  "{\"ok\":true,\"color\":\"%s\",\"autoOffMs\":%u}",
                  resolvedColor,
                  static_cast<unsigned>(enableAutoOff ? kLedAutoOffMs : 0));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebServer::servoGetHandler(httpd_req_t* req)
{
    if (gWebServerInstance == nullptr) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"server unavailable\"}");
    }

    char query[64] = {};
    char servoBuffer[16] = {};
    char angleBuffer[16] = {};
    const std::size_t queryLen = httpd_req_get_url_query_len(req);
    if (queryLen == 0 || queryLen >= sizeof(query) ||
        httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "angle", angleBuffer, sizeof(angleBuffer)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"missing angle\"}");
    }

    long parsedServoId = 1;
    if (httpd_query_key_value(query, "servo", servoBuffer, sizeof(servoBuffer)) == ESP_OK) {
        char* servoParseEnd = nullptr;
        parsedServoId = std::strtol(servoBuffer, &servoParseEnd, 10);
        if (servoParseEnd == servoBuffer || *servoParseEnd != '\0' ||
            parsedServoId < kServoMinId || parsedServoId > kServoMaxId) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_set_type(req, "application/json");
            return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"servo must be 1-4\"}");
        }
    }

    char* parseEnd = nullptr;
    const long parsedAngle = std::strtol(angleBuffer, &parseEnd, 10);
    if (parseEnd == angleBuffer || *parseEnd != '\0' ||
        parsedAngle < kServoMinAngle || parsedAngle > kServoMaxAngle) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"angle must be 0-180\"}");
    }

    const std::uint8_t servoIndex = static_cast<std::uint8_t>(parsedServoId - 1);
    const esp_err_t err = gWebServerInstance->setServoAngle(
        servoIndex, static_cast<std::uint16_t>(parsedAngle));
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"servo update failed\"}");
    }

    char body[96];
    std::snprintf(body, sizeof(body),
                  "{\"ok\":true,\"servo\":%u,\"channel\":%u,\"angle\":%u}",
                  static_cast<unsigned>(parsedServoId),
                  static_cast<unsigned>(servoIndex),
                  static_cast<unsigned>(parsedAngle));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebServer::servoSweepGetHandler(httpd_req_t* req)
{
    if (gWebServerInstance == nullptr) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"server unavailable\"}");
    }

    char query[64] = {};
    char servoBuffer[16] = {};
    std::uint8_t servoIndex = 0;
    long parsedServoId = 1;

    const std::size_t queryLen = httpd_req_get_url_query_len(req);
    if (queryLen > 0 && queryLen < sizeof(query) &&
        httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "servo", servoBuffer, sizeof(servoBuffer)) == ESP_OK) {
        char* servoParseEnd = nullptr;
        parsedServoId = std::strtol(servoBuffer, &servoParseEnd, 10);
        if (servoParseEnd == servoBuffer || *servoParseEnd != '\0' ||
            parsedServoId < kServoMinId || parsedServoId > kServoMaxId) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_set_type(req, "application/json");
            return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"servo must be 1-4\"}");
        }
        servoIndex = static_cast<std::uint8_t>(parsedServoId - 1);
    }

    const esp_err_t err = gWebServerInstance->runServoSelfTest(servoIndex);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"servo self-test failed\"}");
    }

    const std::uint16_t finalAngle = gWebServerInstance->servoAnglesDeg_[servoIndex];
    char body[96];
    std::snprintf(body, sizeof(body),
                  "{\"ok\":true,\"servo\":%u,\"channel\":%u,\"angle\":%u,\"sweep\":true}",
                  static_cast<unsigned>(parsedServoId),
                  static_cast<unsigned>(servoIndex),
                  static_cast<unsigned>(finalAngle));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}
