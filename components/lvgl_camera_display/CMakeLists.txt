# lvgl_camera_display component - pure camera display only

# Source files
set(srcs
    "lvgl_camera_display.cpp"
)

# Include directories
set(include_dirs
    "${COMPONENT_DIR}"
)

# Register the component. esp_driver_ppa is required for the hardware PPA
# pre-resize path that copies a camera frame into a canvas-sized buffer so
# LVGL can render zero-copy when canvas size != camera size.
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS ${include_dirs}
    REQUIRES esp_driver_ppa esp_mm
)
