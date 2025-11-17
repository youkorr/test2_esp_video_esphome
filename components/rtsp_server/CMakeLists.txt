idf_component_register(
    SRCS "rtsp_server.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_h264
        driver
        hal
        esp_driver_h264
)

# Force link H.264 hardware encoder libraries
target_link_libraries(${COMPONENT_LIB} INTERFACE
    idf::esp_h264
    idf::driver
    idf::hal
)
