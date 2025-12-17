#pragma once

#include "esp_cam_sensor.h"

// External declarations for OV02C10 custom formats
// These formats are defined in the OV02C10 driver (ov02c10.c)

#ifdef __cplusplus
extern "C" {
#endif

// Custom format 1280x800 @ 30fps RAW10
extern const esp_cam_sensor_format_t ov02c10_format_1280x800_raw10_30fps;

// Custom format 640x480 @ 30fps RAW10
extern const esp_cam_sensor_format_t ov02c10_format_640x480_raw10_30fps;

// Custom format 800x600 @ 30fps RAW10
extern const esp_cam_sensor_format_t ov02c10_format_800x600_raw10_30fps;

// Custom format 800x480 @ 30fps RAW10
extern const esp_cam_sensor_format_t ov02c10_format_800x480_raw10_30fps;

#ifdef __cplusplus
}
#endif
