#pragma once

#include "esp_cam_sensor.h"

// External declarations for OV02C10 custom formats
// These formats are defined in the OV02C10 driver (ov02c10.c)
//
// Supported resolutions (ALL use FULL SENSOR - NO ZOOM):
//   - 640x480 @ 30fps RAW10 (VGA) - full sensor (0-1935 x 4-1091), ISP downscales
//   - 800x600 @ 30fps RAW10 (SVGA) - full sensor (0-1935 x 4-1091), ISP downscales
//   - 480x640 @ 30fps RAW10 (VGA rotated 270°) - full sensor, rotation enabled
//
// Native full sensor: 1920x1080 @ 30fps RAW10 (100% sensor area)

#ifdef __cplusplus
extern "C" {
#endif

// Custom format 640x480 @ 30fps RAW10 (VGA)
extern const esp_cam_sensor_format_t ov02c10_format_640x480_raw10_30fps;

// Custom format 800x600 @ 30fps RAW10 (SVGA)
extern const esp_cam_sensor_format_t ov02c10_format_800x600_raw10_30fps;

// Custom format 480x640 @ 30fps RAW10 (VGA rotated 270°)
extern const esp_cam_sensor_format_t ov02c10_format_480x640_raw10_30fps_rot270;

#ifdef __cplusplus
}
#endif
