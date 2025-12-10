# lvgl_camera_display component - pure camera display only

# Source files
set(srcs
    "lvgl_camera_display.cpp"
)

# Include directories
set(include_dirs
    "${COMPONENT_DIR}"
)

# Register the component (no detection dependencies)
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS ${include_dirs}
)
