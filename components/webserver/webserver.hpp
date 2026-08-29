#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

class WebServer {
public:
    static WebServer& getInstance();

    esp_err_t start();
    bool isRunning() const;

private:
    static void waitAndStartTask(void* arg);
    static esp_err_t rootGetHandler(httpd_req_t* req);
    static esp_err_t statusGetHandler(httpd_req_t* req);

    esp_err_t startHttpServer();
    static void registerGetHandler(httpd_handle_t server, const char* uri,
                                   esp_err_t (*handler)(httpd_req_t*));

    bool isStarted_ = false;
    httpd_handle_t server_ = nullptr;
};
