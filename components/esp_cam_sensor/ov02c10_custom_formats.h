#pragma once

#include "esp_cam_sensor.h"

// External declarations for OV02C10 custom formats
// These formats are defined in the OV02C10 driver (ov02c10.c)
//
// ⚠️  IMPORTANT: OV02C10 native sensor is 1936×1088 (16:9 ratio)
//     Formats with 4:3 ratio (640×480) crop 25% of horizontal FOV!
//     → Use 640×368 for 98% FOV coverage (recommended)
//
// Supported resolutions (ALL use FULL SENSOR - NO ZOOM):
//   ✅ RECOMMENDED:
//     - 640x368 @ 30fps RAW10 (near 16:9) - BEST FOV (98% coverage), ~2% crop, 16-byte aligned (rotation safe!)
//     - 1920x1080 @ 30fps RAW10 (1080P) - FULL FOV (100% coverage), native resolution, 0% crop
//
//   ⚠️  WITH FOV LOSS:
//     - 640x480 @ 30fps RAW10 (VGA 4:3) - WARNING: 25% horizontal crop (zoom effect 1.33x), only 75% FOV visible!
//     - 1288x728 @ 30fps RAW10 (Near HD 16:9) - full sensor, ISP downscales, optimized 16:9 aspect ratio
//     - 480x640 @ 30fps RAW10 (portrait) - full sensor, rotation enabled
//
// DISABLED (watchdog timeout issue):
//   - 800x600 @ 30fps RAW10 (SVGA 4:3) - DISABLED: causes watchdog timeout after 60s
//     → Use 640x368 (better FOV) or 640x480 (same 4:3 ratio) instead
//     → See OV02C10_800x600_ISSUE.md for details
//
// See OV02C10_640x480_ZOOM_FIX.md for detailed comparison of formats

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
