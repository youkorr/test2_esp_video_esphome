idf_component_register(
    SRCS "rtsp_server.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_h264
        driver
        lwip
)

# Link ESP-IDF H.264 encoder library
# (driver, soc, hal dependencies are handled by esp_h264 component)
target_link_libraries(${COMPONENT_LIB} INTERFACE
    idf::esp_h264
)

