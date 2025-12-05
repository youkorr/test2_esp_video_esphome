# ESP H.264 Encoder Component

set(component_srcs
    "port/src/esp_h264_alloc.c"
    "port/src/esp_h264_alloc_less_than_5_3.c"
    "port/src/esp_h264_cache.c"
    "sw/src/h264_color_convert.c"
    "sw/src/esp_h264_enc_sw_param.c"
    "sw/src/esp_h264_dec_sw.c"
    "sw/src/esp_h264_enc_single_sw.c"
    "hw/src/esp_h264_enc_single_hw.c"
    "hw/src/esp_h264_enc_dual_hw.c"
    "hw/src/esp_h264_enc_hw_param.c"
    "hw/src/h264_nal.c"
    "hw/src/h264_rc.c"
    "hw/hal/esp32p4/h264_hal.c"
    "hw/hal/esp32p4/h264_dma_hal.c"
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
    soc
    hal
)

set(component_priv_include_dirs
    "hw/src"
    "hw/hal/esp32p4"
    "hw/soc/esp32p4"
    "sw/src"
)

idf_component_register(
    SRCS ${component_srcs}
    INCLUDE_DIRS ${component_include_dirs}
    PRIV_INCLUDE_DIRS ${component_priv_include_dirs}
    REQUIRES ${component_requires}
)

# Enable dual-core H.264 decoding for 30-50% performance boost
# These flags activate dual-task mode in esp_h264_dec_sw.c
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    CONFIG_ESP_H264_DUAL_TASK=1
    CONFIG_ESP_H264_DUAL_TASK_CORE=1
    CONFIG_ESP_H264_DUAL_TASK_PRIORITY=5
)

message(STATUS "ESP H.264 component registered with DUAL_TASK enabled")

# Link prebuilt optimized decoder libraries
idf_build_get_property(target IDF_TARGET)

if("${target}" STREQUAL "esp32p4")
    # Add include directories for decoder libraries
    target_include_directories(${COMPONENT_LIB} PRIVATE
        "sw/libs/tinyh264_inc"
        "sw/libs/openh264_inc"
    )

    # NOTE: H.264 library linking is handled by PlatformIO build scripts
    # (simple_video_player_build.py, network_camera_build.py, etc.)
    # Do NOT link it here in CMakeLists.txt to avoid duplicate symbols!
    #
    # Link prebuilt openh264 library (supports ALL H.264 profiles: Baseline, Main, High, etc.)
    # add_prebuilt_library(openh264_lib "${CMAKE_CURRENT_SOURCE_DIR}/sw/libs/${target}/libopenh264.a"
    #                     PRIV_REQUIRES ${COMPONENT_NAME})
    # target_link_libraries(${COMPONENT_LIB} PUBLIC openh264_lib)
    # message(STATUS "ESP H.264: Linked optimized openh264 library for ${target} (supports Baseline/Main/High profiles)")

    # Alternative: Use tinyh264 (smaller but ONLY Baseline profile)
    # Uncomment the lines below and comment out openh264 above to use tinyh264:
    # add_prebuilt_library(tinyh264_lib "${CMAKE_CURRENT_SOURCE_DIR}/sw/libs/${target}/libtinyh264.a"
    #                     PRIV_REQUIRES ${COMPONENT_NAME})
    # target_link_libraries(${COMPONENT_LIB} INTERFACE tinyh264_lib)
    # message(STATUS "ESP H.264: Linked optimized tinyh264 library for ${target}")
endif()


