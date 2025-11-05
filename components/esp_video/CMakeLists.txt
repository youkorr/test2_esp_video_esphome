# ESP-Video Component CMakeLists.txt
# Ce fichier indique à ESP-IDF comment compiler le composant esp_video

set(component_srcs
    "esp_video_component.cpp"
    "src/esp_video.c"
    "src/esp_video_init.c"
    "src/esp_video_buffer.c"
    "src/esp_video_cam.c"
    "src/esp_video_mman.c"
    "src/esp_video_vfs.c"
    "src/esp_video_ioctl.c"
    "src/device/esp_video_csi_device.c"
)

# Ajouter conditionnellement les devices selon les flags de configuration
if(CONFIG_ESP_VIDEO_ENABLE_ISP OR DEFINED ENV{ESP_VIDEO_ISP_ENABLED})
    list(APPEND component_srcs
        "src/device/esp_video_isp_device.c"
        "src/esp_video_isp_pipeline.c"
    )
    message(STATUS "ESP-Video: ISP device sources included")
endif()

if(CONFIG_ESP_VIDEO_ENABLE_HW_JPEG_VIDEO_DEVICE OR DEFINED ENV{ESP_VIDEO_JPEG_ENABLED})
    list(APPEND component_srcs
        "src/device/esp_video_jpeg_device.c"
    )
    message(STATUS "ESP-Video: JPEG device sources included")
endif()

if(CONFIG_ESP_VIDEO_ENABLE_HW_H264_VIDEO_DEVICE OR DEFINED ENV{ESP_VIDEO_H264_ENABLED})
    list(APPEND component_srcs
        "src/device/esp_video_h264_device.c"
    )
    message(STATUS "ESP-Video: H.264 device sources included")
endif()

# Optionnel : DVP, SPI, USB UVC (pas utilisés pour MIPI-CSI)
# list(APPEND component_srcs
#     "src/device/esp_video_dvp_device.c"
#     "src/device/esp_video_spi_device.c"
#     "src/device/esp_video_usb_uvc_device.c"
# )

# Répertoires d'include
set(component_include_dirs
    "."
    "include"
    "include/linux"
    "include/sys"
    "src"
    "src/device"
    "private_include"
)

# Composants ESP-IDF requis
set(component_requires
    driver
    esp_driver_isp
    esp_driver_cam
    esp_mm
    spi_flash
    vfs
)

set(component_priv_requires
    # Composants ESP-IDF supplémentaires si nécessaires
)

# Enregistrer le composant
idf_component_register(
    SRCS ${component_srcs}
    INCLUDE_DIRS ${component_include_dirs}
    REQUIRES ${component_requires}
    PRIV_REQUIRES ${component_priv_requires}
)

# Flags de compilation spécifiques au composant
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-missing-field-initializers
)

message(STATUS "ESP-Video component registered with ${list(LENGTH component_srcs)} source files")
