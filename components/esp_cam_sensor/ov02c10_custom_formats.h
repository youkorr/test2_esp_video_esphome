#pragma once

#include "esp_cam_sensor.h"

// External declarations for OV02C10 custom formats
// These formats are defined in the OV02C10 driver (ov02c10.c)
//
// Supported resolutions (ALL use FULL SENSOR - NO ZOOM):
//   - 1288x728 @ 30fps RAW10 (Near HD 16:9) - full sensor, ISP downscales, optimized 16:9 aspect ratio
//   - 640x480 @ 30fps RAW10 (VGA 4:3) - full sensor (0-1935 x 4-1091), ISP downscales, 25% horizontal crop
//   - 640x368 @ 30fps RAW10 (near 16:9) - full sensor (0-1935 x 4-1091), ISP downscales, ~2% crop, 16-byte aligned (rotation safe!)
//   - 480x640 @ 30fps RAW10 (VGA rotated 270°) - full sensor, rotation enabled
//   - 1920x1080 @ 30fps RAW10 (1080P) - full sensor (0-1935 x 4-1091), native resolution, 0% crop
//
// DISABLED (watchdog timeout issue):
//   - 800x600 @ 30fps RAW10 (SVGA 4:3) - DISABLED: causes watchdog timeout after 60s
//     → Use 640x480 (same 4:3 ratio) or 1288x728 (16:9) instead
//     → See OV02C10_800x600_ISSUE.md for details
//
// Native full sensor: 1920x1080 @ 30fps RAW10 (100% sensor area, 16:9 aspect ratio)

#ifdef __cplusplus
extern "C" {
#endif

// Custom format 1288x728 @ 30fps RAW10 (Near HD 16:9)
extern const esp_cam_sensor_format_t ov02c10_format_1288x728_raw10_30fps;

// Custom format 640x480 @ 30fps RAW10 (VGA)
extern const esp_cam_sensor_format_t ov02c10_format_640x480_raw10_30fps;

// DISABLED: 800x600 causes watchdog timeout - use 640x480 or 1288x728 instead
// See OV02C10_800x600_ISSUE.md
// extern const esp_cam_sensor_format_t ov02c10_format_800x600_raw10_30fps;

// Custom format 480x640 @ 30fps RAW10 (VGA rotated 270°)
extern const esp_cam_sensor_format_t ov02c10_format_480x640_raw10_30fps_rot270;

// Custom format 640x368 @ 30fps RAW10 (near 16:9 - Best FOV, rotation safe, 16-byte aligned!)
extern const esp_cam_sensor_format_t ov02c10_format_640x368_raw10_30fps;

// Custom format 1920x1080 @ 30fps RAW10 (1080P - Full HD)
extern const esp_cam_sensor_format_t ov02c10_format_1920x1080_raw10_30fps;

#ifdef __cplusplus
}
#endif
