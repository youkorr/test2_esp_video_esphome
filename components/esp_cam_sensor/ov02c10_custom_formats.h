/*
 * OV02C10 Custom Format Configurations
 * Support for non-standard resolutions: 640x480, 800x600, 480x640, 640x368
 *
 * These custom formats are optimized for small LCD displays and rotation.
 * Original formats (1288x728, 1920x1080) remain in ov02c10.c driver.
 */

#pragma once

#include <stdint.h>
#include "esp_cam_sensor_types.h"

/* Include OV02C10 settings for register arrays and clock rates */
#include "sensor/ov02c10/private_include/ov02c10_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

// ⚠️  IMPORTANT: OV02C10 native sensor is 1936×1088 (16:9 ratio)
//     Formats with 4:3 ratio (640×480, 800x600) crop 25% of horizontal FOV!
//     → Use 640×368 for 98% FOV coverage (recommended)
//
// Supported resolutions (ALL use FULL SENSOR - NO ZOOM):
//   ✅ RECOMMENDED:
//     - 640x368 @ 30fps RAW10 (near 16:9) - BEST FOV (98% coverage), ~2% crop, 16-byte aligned (rotation safe!)
//     - 1920x1080 @ 30fps RAW10 (1080P) - FULL FOV (100% coverage), native resolution, 0% crop
//     - 1288x728 @ 30fps RAW10 (Near HD 16:9) - full sensor, ISP downscales, optimized 16:9 aspect ratio
//
//   ⚠️  WITH FOV LOSS (4:3 crop):
//     - 640x480 @ 30fps RAW10 (VGA 4:3) - WARNING: 25% horizontal crop (zoom effect 1.33x), only 75% FOV visible!
//     - 800x600 @ 30fps RAW10 (SVGA 4:3) - 25% horizontal crop, based on working 640x480 config
//     - 480x640 @ 30fps RAW10 (portrait) - full sensor, rotation enabled
//
// DISABLED (watchdog timeout issue):
//   - 960x540 @ 30fps RAW10 (qHD 16:9) - DISABLED: causes persistent watchdog timeout
//     → Use 640x368 (better FOV) or 1288x728 (HD) instead
//
// See OV02C10_640x480_ZOOM_FIX.md for detailed comparison of formats

// ============================================================================
// ISP info for custom formats (shared by most custom resolutions)
// ============================================================================

static const esp_cam_sensor_isp_info_t ov02c10_custom_isp_info = {
    .isp_v1_info = {
        .version = SENSOR_ISP_INFO_VERSION_DEFAULT,
        .pclk = 81666700,
        .vts = 1164,
        .hts = 2280,
        .gain_def = 0x01,
        .exp_def = 0x46c,
        .bayer_type = ESP_CAM_SENSOR_BAYER_GBRG,
    }
};

// ============================================================================
// Configuration 1: VGA 640x480 @ 30fps RAW10 (4:3 with 25% horizontal crop)
// ============================================================================

static const esp_cam_sensor_format_t ov02c10_format_640x480_raw10_30fps = {
    .name = "MIPI_1lane_24Minput_RAW10_640x480_30fps",
    .format = ESP_CAM_SENSOR_PIXFORMAT_RAW10,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .xclk = 24000000,
    .width = 640,
    .height = 480,
    .regs = ov02c10_input_24M_MIPI_1lane_raw10_640x480_30fps,
    .regs_size = ARRAY_SIZE(ov02c10_input_24M_MIPI_1lane_raw10_640x480_30fps),
    .fps = 30,
    .isp_info = &ov02c10_custom_isp_info,
    .mipi_info = {
        .mipi_clk = OV02C10_MIPI_CSI_LINE_RATE_800x640_50FPS,
        .lane_num = 1,
        .line_sync_en = false,
    },
    .reserved = NULL,
};

// ============================================================================
// Configuration 2: SVGA 800x600 @ 30fps RAW10 (4:3 with 25% horizontal crop)
// Based on working 640x480 configuration
// ============================================================================

static const esp_cam_sensor_format_t ov02c10_format_800x600_raw10_30fps = {
    .name = "MIPI_1lane_24Minput_RAW10_800x600_30fps",
    .format = ESP_CAM_SENSOR_PIXFORMAT_RAW10,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .xclk = 24000000,
    .width = 800,
    .height = 600,
    .regs = ov02c10_input_24M_MIPI_1lane_raw10_800x600_30fps,
    .regs_size = ARRAY_SIZE(ov02c10_input_24M_MIPI_1lane_raw10_800x600_30fps),
    .fps = 30,
    .isp_info = &ov02c10_custom_isp_info,
    .mipi_info = {
        .mipi_clk = OV02C10_MIPI_CSI_LINE_RATE_800x640_50FPS,
        .lane_num = 1,
        .line_sync_en = false,
    },
    .reserved = NULL,
};

// ============================================================================
// Configuration 3: Portrait 480x640 @ 30fps RAW10 with rotation 270°
// ============================================================================

static const esp_cam_sensor_format_t ov02c10_format_480x640_raw10_30fps_rot270 = {
    .name = "MIPI_1lane_24Minput_RAW10_480x640_30fps_rot270",
    .format = ESP_CAM_SENSOR_PIXFORMAT_RAW10,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .xclk = 24000000,
    .width = 480,  // Portrait 480×640, LVGL handles rotation
    .height = 640,
    .regs = ov02c10_input_24M_MIPI_1lane_raw10_480x640_30fps_rot270,
    .regs_size = ARRAY_SIZE(ov02c10_input_24M_MIPI_1lane_raw10_480x640_30fps_rot270),
    .fps = 30,
    .isp_info = &ov02c10_custom_isp_info,
    .mipi_info = {
        .mipi_clk = OV02C10_MIPI_CSI_LINE_RATE_800x640_50FPS,
        .lane_num = 1,
        .line_sync_en = false,
    },
    .reserved = NULL,
};

// ============================================================================
// Configuration 4: 640x368 @ 30fps RAW10 (near 16:9, 16-byte aligned)
// BEST FOV: 98% coverage, only ~2% crop, rotation safe!
// ============================================================================

static const esp_cam_sensor_format_t ov02c10_format_640x368_raw10_30fps = {
    .name = "MIPI_1lane_24Minput_RAW10_640x368_30fps",
    .format = ESP_CAM_SENSOR_PIXFORMAT_RAW10,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .xclk = 24000000,
    .width = 640,
    .height = 368,
    .regs = ov02c10_input_24M_MIPI_1lane_raw10_640x368_30fps,
    .regs_size = ARRAY_SIZE(ov02c10_input_24M_MIPI_1lane_raw10_640x368_30fps),
    .fps = 30,
    .isp_info = &ov02c10_custom_isp_info,
    .mipi_info = {
        .mipi_clk = OV02C10_MIPI_CSI_LINE_RATE_800x640_50FPS,
        .lane_num = 1,
        .line_sync_en = false,
    },
    .reserved = NULL,
};

// ============================================================================
// Original formats (defined in ov02c10.c driver)
// ============================================================================

// Original format 1288x728 @ 30fps RAW10 (Near HD 16:9)
extern const esp_cam_sensor_format_t ov02c10_format_1288x728_raw10_30fps;

// Original format 1920x1080 @ 30fps RAW10 (1080P - Full HD)
extern const esp_cam_sensor_format_t ov02c10_format_1920x1080_raw10_30fps;

#ifdef __cplusplus
}
#endif
