set(include_dirs "include")

# CRITICAL FIX: Compile ALL IPA source files for ESPHome/PlatformIO
set(srcs "src/version.c"
         "src/esp_ipa_detect_stubs.c"
         "src/esp_ipa_json_loader.c")

# NOTE: JSON embedding is handled by esp_video_build.py for PlatformIO compatibility
# The Python script generates C wrappers with symbols like ov02c10_ipa_config_json_start

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






