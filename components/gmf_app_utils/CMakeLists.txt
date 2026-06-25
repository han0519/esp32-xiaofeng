set(APP_UTILS_SRCS
    "src/esp_gmf_app_cli.c"
    "src/esp_gmf_app_sys.c"
    "src/esp_gmf_app_unit_test.c"
)
# esp_gmf_app_setup_peripheral.c excluded — requires protocol_examples_common (WiFi/SD/etc)
# EXTRA_SRCS from IDF protocol_examples_common excluded — not needed for BT-only builds

idf_component_register(
    SRCS ${APP_UTILS_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES "nvs_flash" "unity" "gmf_core" "esp_board_manager" "esp_codec_dev" "console"
    PRIV_REQUIRES esp_netif lwip
)
