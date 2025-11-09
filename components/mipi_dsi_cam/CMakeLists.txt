# mipi_dsi_cam component with imlib library support

# Source files for mipi_dsi_cam
set(srcs
    "mipi_dsi_cam.cpp"
)

# Include directories
set(include_dirs
    "${COMPONENT_DIR}"
    "${COMPONENT_DIR}/../imlib/include"  # imlib headers
)

# Register the component
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS ${include_dirs}
    REQUIRES esp_video imlib
)
