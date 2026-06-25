set(LOADER_SRCS
    "src/gmf_loader_setup_all_defaults.c"
    "src/gmf_loader_setup_audio_codec.c"
    "src/gmf_loader_setup_audio_effects.c"
    "src/gmf_loader_setup_audio_muxer.c"
    "src/gmf_loader_setup_io.c"
    "src/gmf_loader_setup_misc.c"
)
# AI audio and video source files excluded — they require unavailable components
# (gmf_loader_setup_ai_audio.c, gmf_loader_setup_video_codec.c, gmf_loader_setup_video_effects.c)

idf_component_register(
    SRCS ${LOADER_SRCS}
    INCLUDE_DIRS "./include"
    REQUIRES "mbedtls" "gmf_core" "gmf_io" "gmf_audio" "gmf_misc" "esp_audio_codec" "esp_codec_dev" "esp_muxer"
)
