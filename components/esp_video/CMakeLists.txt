set(srcs "src/esp_video_buffer.c"
         "src/esp_video_init.c"
         "src/esp_video_ioctl.c"
         "src/esp_video_mman.c"
         "src/esp_video_vfs.c"
         "src/esp_video.c"
         "src/esp_video_cam.c")

set(include_dirs "include")
set(priv_include_dirs "private_include")
set(priv_requires "vfs" "spi_flash" "esp_hw_support")
set(requires "esp_driver_cam" "esp_driver_isp" "esp_cam_sensor" "esp_driver_jpeg" "esp_sccb_intf")

if(CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE)
    list(APPEND srcs "src/device/esp_video_csi_device.c")
endif()

if(CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE)
    list(APPEND srcs "src/device/esp_video_dvp_device.c")
endif()

if(CONFIG_ESP_VIDEO_ENABLE_JPEG_VIDEO_DEVICE)
    list(APPEND srcs "src/device/esp_video_jpeg_device.c")
endif()

if(CONFIG_ESP_VIDEO_ENABLE_ISP)
    list(APPEND srcs "src/device/esp_video_isp_device.c")

    # CRITICAL FIX: Always compile ISP pipeline controller for JSON IPA loading
    # This is safe for all sensors - the code detects sensor type at runtime
    list(APPEND srcs "src/esp_video_isp_pipeline.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dirs}
                       PRIV_INCLUDE_DIRS ${priv_include_dirs}
                       PRIV_REQUIRES ${priv_requires}
                       REQUIRES ${requires})

# CRITICAL FIX: Always require esp_ipa for JSON IPA loading
# This is safe for all sensors - the code detects sensor type at runtime
idf_component_optional_requires(PRIVATE "esp_ipa")


