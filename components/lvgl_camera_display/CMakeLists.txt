# lvgl_camera_display component with face and pedestrian detection support

# Source files
set(srcs
    "lvgl_camera_display.cpp"
)

# Include directories
set(include_dirs
    "${COMPONENT_DIR}"
)

# Register the component with ESP-IDF dependencies
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS ${include_dirs}
    REQUIRES human_face_detect pedestrian_detect human_face_recognition esp-dl
)
