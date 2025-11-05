# ESP H.264 Encoder Component

set(component_srcs
    "port/src/esp_h264_alloc.c"
    "port/src/esp_h264_alloc_less_than_5_3.c"
    "port/src/esp_h264_cache.c"
    "sw/src/h264_color_convert.c"
    "sw/src/esp_h264_enc_sw_param.c"
    "sw/src/esp_h264_dec_sw.c"
    "sw/src/esp_h264_enc_single_sw.c"
    "interface/include/src/esp_h264_enc_param.c"
    "interface/include/src/esp_h264_enc_param_hw.c"
    "interface/include/src/esp_h264_enc_dual.c"
    "interface/include/src/esp_h264_dec_param.c"
    "interface/include/src/esp_h264_version.c"
    "interface/include/src/esp_h264_dec.c"
    "interface/include/src/esp_h264_enc_single.c"
)

set(component_include_dirs
    "interface/include"
    "port/include"
    "sw/include"
    "hw/include"
)

set(component_requires
    driver
)

idf_component_register(
    SRCS ${component_srcs}
    INCLUDE_DIRS ${component_include_dirs}
    REQUIRES ${component_requires}
)

message(STATUS "ESP H.264 component registered")


