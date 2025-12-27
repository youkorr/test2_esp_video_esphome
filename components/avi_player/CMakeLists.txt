idf_component_register(SRC_DIRS "."
                       INCLUDE_DIRS "include"
                       REQUIRES esp_timer esp_image_effects)

# Link against esp_image_effects prebuilt library
target_link_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../esp_image_effects/lib/${CONFIG_IDF_TARGET}")
target_link_libraries(${COMPONENT_LIB} PUBLIC esp_image_effects)

include(package_manager)
cu_pkg_define_version(${CMAKE_CURRENT_LIST_DIR})

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-implicit-fallthrough")
