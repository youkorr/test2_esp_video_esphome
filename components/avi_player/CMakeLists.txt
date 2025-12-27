idf_component_register(SRC_DIRS "."
                       INCLUDE_DIRS "include"
                       REQUIRES esp_timer esp_image_effects)

include(package_manager)
cu_pkg_define_version(${CMAKE_CURRENT_LIST_DIR})

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-implicit-fallthrough")
