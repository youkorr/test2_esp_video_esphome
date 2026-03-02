idf_component_register(
    SRCS "esp_extractor_reg.c"
    INCLUDE_DIRS "include"
)

add_prebuilt_library(extractor "${CMAKE_CURRENT_SOURCE_DIR}/lib/${CONFIG_IDF_TARGET}/libesp_extractor.a")
target_link_libraries(${COMPONENT_LIB} PRIVATE extractor)
