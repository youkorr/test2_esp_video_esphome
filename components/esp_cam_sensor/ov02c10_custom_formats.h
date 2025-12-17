#pragma once

#include "esp_cam_sensor.h"

// External declarations for OV02C10 custom formats
// These formats are defined in the OV02C10 driver (ov02c10.c)
//
// Supported resolutions:
//   - 640x480 @ 30fps RAW10 (VGA) - centered crop from native sensor
//   - 800x600 @ 30fps RAW10 (SVGA) - centered crop from native sensor
//
// Native resolution: 1288x728 @ 30fps RAW10

#ifdef __cplusplus
extern "C" {
#endif

// Custom format 640x480 @ 30fps RAW10 (VGA)
extern const esp_cam_sensor_format_t ov02c10_format_640x480_raw10_30fps;

// Custom format 800x600 @ 30fps RAW10 (SVGA)
extern const esp_cam_sensor_format_t ov02c10_format_800x600_raw10_30fps;

#ifdef __cplusplus
}
#endif
