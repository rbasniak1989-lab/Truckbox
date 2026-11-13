idf_component_register(
    SRCS "tb_web.c" "tb_image_policy.c" "tb_crash.c"
    INCLUDE_DIRS "."
    REQUIRES tb_base esp_http_server esp_http_client app_update bootloader_support esp_app_format esp_timer json espcoredump esp_partition
    EMBED_TXTFILES "index.html"
)
