set(link_sources "")
if(CONFIG_TB_ROLE_LINK)
    set(link_sources "tb_link.c" "tb_nmea.c" "tb_gps_outbox.c")
endif()

# Requirements must be visible in IDF's early dependency pass, before Kconfig.
idf_component_register(
    SRCS ${link_sources}
    INCLUDE_DIRS "."
    REQUIRES tb_base esp_driver_gpio esp_driver_uart esp_netif esp_event esp_timer
             esp_http_client esp_http_server json lwip mbedtls esp_modem
             fatfs wear_levelling esp_partition esp_hw_support
)
