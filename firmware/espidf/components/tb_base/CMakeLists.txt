idf_component_register(SRCS "config.c" "network.c" "runtime.c" "tb_json.c" "lite_wire.c" "fuel_windows.c" INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server json esp_timer esp_app_format app_update esp_system esp_driver_gpio lwip bootloader_support
    PRIV_REQUIRES mdns)
if(CONFIG_TB_CORE_LITE AND TRUCKBOX_REVB1_CONFIRMED_CORE_UID)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "TB_LITE_CONFIRMED_CORE_UID=\"${TRUCKBOX_REVB1_CONFIRMED_CORE_UID}\"")
endif()
