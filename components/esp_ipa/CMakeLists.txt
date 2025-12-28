set(include_dirs "include")

# CRITICAL FIX: Compile ALL IPA source files for ESPHome/PlatformIO
set(srcs "src/version.c"
         "src/esp_ipa_detect_stubs.c"
         "src/esp_ipa_json_loader.c")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dirs}
                       REQUIRES cJSON)

idf_build_get_property(idf_target IDF_TARGET)

# Link prebuilt IPA library
add_prebuilt_library(prebuilt "lib/${idf_target}/libesp_ipa.a"
                     PRIV_REQUIRES esp_timer)
target_link_libraries(${COMPONENT_LIB} PRIVATE prebuilt)

# NOTE: No need for CONFIG-based -u flags
# esp_ipa_detect_stubs.c already references all IPA algorithms unconditionally
# This ensures AWB, AGC, denoise, sharpen, gamma, and CCM are always available

# ============================================================================
# EMBED IPA JSON CONFIGURATION FILES
# ============================================================================
# These JSON files contain sensor-specific ISP tuning parameters (CCM, AWB, etc.)

set(ESP_CAM_SENSOR_DIR "${CMAKE_CURRENT_LIST_DIR}/../esp_cam_sensor")

# OV02C10 JSON configuration
if(EXISTS "${ESP_CAM_SENSOR_DIR}/sensor/ov02c10/cfg/ov02c10_default.json")
    target_add_binary_data(${COMPONENT_LIB}
        "${ESP_CAM_SENSOR_DIR}/sensor/ov02c10/cfg/ov02c10_default.json"
        RENAME_TO "ov02c10_ipa_config.json"
        BINARY)
    message(STATUS "ESP-IPA: Embedded OV02C10 JSON configuration")
else()
    message(WARNING "ESP-IPA: OV02C10 JSON not found at ${ESP_CAM_SENSOR_DIR}/sensor/ov02c10/cfg/ov02c10_default.json")
endif()

# OV5647 JSON configuration
if(EXISTS "${ESP_CAM_SENSOR_DIR}/sensor/ov5647/cfg/ov5647_default.json")
    target_add_binary_data(${COMPONENT_LIB}
        "${ESP_CAM_SENSOR_DIR}/sensor/ov5647/cfg/ov5647_default.json"
        RENAME_TO "ov5647_ipa_config.json"
        BINARY)
    message(STATUS "ESP-IPA: Embedded OV5647 JSON configuration")
else()
    message(WARNING "ESP-IPA: OV5647 JSON not found")
endif()






