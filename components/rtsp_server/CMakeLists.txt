idf_component_register(
    SRCS "rtsp_server.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_h264
        driver
        esp_driver_gpio
        esp_driver_h264
        hal
        lwip
)

# Link ESP-IDF H.264 hardware encoder libraries
target_link_libraries(${COMPONENT_LIB} INTERFACE
    idf::esp_h264
    idf::driver
    idf::hal
)

