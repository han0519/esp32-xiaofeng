cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR "IDF_PATH not defined")
endif()

set(BASE "c:/Users/han/Desktop/audio")
set(PROJ "${CMAKE_CURRENT_LIST_DIR}")

# Local GMF components
set(GMF_ROOT "${BASE}/esp-gmf-main/esp-gmf-main")
set(BT_AUDIO  "${BASE}/espressif__esp_bt_audio-v1.0.0/esp_bt_audio")
set(BMGR      "${BASE}/esp-board-manager-main/esp-board-manager-main")

set(EXTRA_COMPONENT_DIRS
    # GMF Core & Elements
    "${GMF_ROOT}/gmf_core"
    "${GMF_ROOT}/elements/gmf_io"
    "${GMF_ROOT}/elements/gmf_audio"
    "${GMF_ROOT}/elements/gmf_misc"
    "${GMF_ROOT}/packages/esp_bt_audio"
    "${GMF_ROOT}/packages/gmf_loader"
    "${GMF_ROOT}/packages/gmf_app_utils"
    "${GMF_ROOT}/packages/gmf_fft"
    "${GMF_ROOT}/packages/esp_asrc"

    # Audio Codec & Codec Dev
    "${BASE}/espressif__esp_audio_codec-v2.5.0/esp_audio_codec"
    "${BASE}/espressif__esp_codec_dev-v1.5.10/esp_codec_dev"

    # Audio Effects & Muxer
    "${BASE}/espressif__esp_audio_effects-v1.3.0_1/esp_audio_effects"
    "${BASE}/espressif__esp_muxer-v1.2.2/esp_muxer"

    # Board Manager + Board definitions
    "${BMGR}/esp_board_manager"
    "${BMGR}/esp_boards"

    # LVGL + Display + JPEG (local components)
    "${PROJ}/components/lvgl"
    "${PROJ}/components/esp_lvgl_port"
    "${PROJ}/components/esp_jpeg"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_s31_korvo_bt)
