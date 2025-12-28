set(include_dirs "include")

# CRITICAL FIX: Compile ALL IPA source files for ESPHome/PlatformIO
set(srcs "src/version.c"
         "src/esp_ipa_detect_stubs.c"
         "src/esp_ipa_json_loader.c")

# Embed JSON configuration files from local cfg/ directory
# Symbols created: _binary_cfg_<filename>_json_start / _binary_cfg_<filename>_json_end
set(embed_files "cfg/ov02c10_default.json"
                "cfg/ov5647_default.json")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dirs}
                       EMBED_FILES ${embed_files}
                       REQUIRES cJSON)

idf_build_get_property(idf_target IDF_TARGET)

# Link prebuilt IPA library
add_prebuilt_library(prebuilt "lib/${idf_target}/libesp_ipa.a"
                     PRIV_REQUIRES esp_timer)
target_link_libraries(${COMPONENT_LIB} PRIVATE prebuilt)

# NOTE: No need for CONFIG-based -u flags
# esp_ipa_detect_stubs.c already references all IPA algorithms unconditionally
# This ensures AWB, AGC, denoise, sharpen, gamma, and CCM are always available






