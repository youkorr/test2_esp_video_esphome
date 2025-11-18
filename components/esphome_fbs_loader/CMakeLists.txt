idf_component_register(
    SRCS "esphome_fbs_loader.cpp"
    INCLUDE_DIRS "."
    REQUIRES "esp-dl"
)

# Link against fbs_loader library
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-L${CMAKE_CURRENT_SOURCE_DIR}/../esp-dl/fbs_loader/lib/esp32p4"
    "-lfbs_model"
)

# Add include directories for fbs_loader
target_include_directories(${COMPONENT_LIB} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../esp-dl/fbs_loader/include"
)
