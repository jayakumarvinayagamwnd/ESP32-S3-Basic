#include "webserver.hpp"

#include <cstdio>

#include "WifiManager.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "index_html.hpp"

namespace {
constexpr char kLogTag[] = "webserver";
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
