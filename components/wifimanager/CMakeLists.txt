idf_component_register(
    SRCS "WifiManager.cpp"
    INCLUDE_DIRS "." "../../include"
    REQUIRES esp_wifi esp_event esp_netif nvs_flash
)
