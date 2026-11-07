idf_component_register(SRCS "config.c" "network.c" "runtime.c" INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server json esp_timer esp_app_format app_update esp_system esp_driver_gpio lwip bootloader_support
    PRIV_REQUIRES mdns)
