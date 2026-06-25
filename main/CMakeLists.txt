set(reqs "bt" "nvs_flash" "console"
    "esp_board_manager" "esp_codec_dev"
    "esp_bt_audio" "esp_audio_codec"
    "gmf_core" "gmf_io" "gmf_audio" "gmf_misc"
    "gmf_loader" "gmf_app_utils"
    "lvgl" "esp_lvgl_port" "esp_jpeg")

# Main sources + BT UI sources (LVGL touchscreen)
set(srcs "main.c" "pool_reg.c" "cmd_reg.c" "stream_proc.c")
set(srcs ${srcs} "bt_ui/bt_ui.c")
set(srcs ${srcs} "bt_ui/fonts/lv_font_notosanssc_regular_28.c")
set(srcs ${srcs} "bt_ui/images/cis_stream_icon.c")
set(srcs ${srcs} "bt_ui/images/bis_stream_icon.c")
set(srcs ${srcs} "bt_ui/images/speakers_dark_dark_small.c")
set(srcs ${srcs} "bt_ui/images/speakers_dark_light_small.c")
set(srcs ${srcs} "bt_ui/images/speakers_light_dark_small.c")

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS "." "bt_ui"
                    REQUIRES ${reqs}
                    EMBED_FILES
                        "bt_ui/__static/bis_icon.png"
                        "bt_ui/__static/cis_icon.png"
                        "bt_ui/__static/speakers_dark_dark.png"
                        "bt_ui/__static/speakers_dark_light.png"
                        "bt_ui/__static/speakers_light_dark.png"
                    )

# Suppress overflow warning on auto-generated font file (LVGL 20-bit bitmap_index limit)
set_source_files_properties(
    "bt_ui/fonts/lv_font_notosanssc_regular_28.c"
    PROPERTIES COMPILE_FLAGS "-Wno-overflow"
)
