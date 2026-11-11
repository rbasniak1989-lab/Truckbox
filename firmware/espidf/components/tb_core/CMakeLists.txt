set(core_sources)
if(NOT CONFIG_TB_ROLE_LINK)
    list(APPEND core_sources "core.c" "capture.c" "store.c" "store_codec.c" "protocols.c" "fram.c" "telemetry.c" "tbx_j1939.c" "tbx_j1939_tp.c" "tbx_trip.c" "tbx_fuel_account.c" "tbx_fuel.c" "tbx_j1708_active.c" "tbx_j1708_field.c" "tbx_j1708_rx_guard.c" "tbx_j1587_bridge.c"
        "../../../j1708_17pid/src/tbx_j1587_decoder.c"
        "../../../j1708_17pid/src/tbx_j1587_passive.c"
        "../../../j1708_17pid/src/tbx_j1587_passive_full.c"
        "../../../j1708_17pid/src/tbx_j1708_requests.c")
endif()
idf_component_register(SRCS ${core_sources}
    INCLUDE_DIRS "." "../../../j1708_17pid/src"
    REQUIRES tb_base driver fatfs sdmmc esp_partition esp_timer esp_http_client nvs_flash json wear_levelling lwip)

if(NOT CONFIG_TB_ROLE_LINK)
    target_compile_options(${COMPONENT_LIB} PRIVATE -fstack-usage)
endif()
