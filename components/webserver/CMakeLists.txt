idf_component_register(
    SRCS "webserver.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_http_server wifimanager ledmanager servomanager
)

