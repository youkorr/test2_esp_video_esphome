/*
 * OV02C10 Custom Format Configurations
 * Support for VGA and SVGA resolutions: 640x480 and 800x600
 *
 * NOTE: This file is included by ov02c10.c which already defines ov02c10_reginfo_t
 * Do not include ov02c10_types.h here to avoid path issues
 */

#pragma once

#include <stdint.h>
#include "esp_cam_sensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

// NOTE: ov02c10_reginfo_t is defined in ov02c10_types.h
// This file MUST be included after ov02c10_settings.h in ov02c10.c

// ============================================================================
// Configuration 1 : 640x480 (VGA) @ 30fps RAW10
// ============================================================================
// Centered crop from 1920x1080 sensor area
// Crop calculation: startX=(1920-640)/2=640, startY=(1080-480)/2=300

static const ov02c10_reginfo_t ov02c10_640x480_raw10_30fps[] = {
    { .reg = 0x0103, .val = 0x01 },  // Software reset
    { .reg = 0x0100, .val = 0x00 },  // Standby
    { .reg = 0x0302, .val = 0x32 },  // PLL multiplier
    { .reg = 0x030e, .val = 0x02 },  // PLL divider
    { .reg = 0x3808, .val = 0x02 },  // H output size MSB (640)
    { .reg = 0x3809, .val = 0x80 },  // H output size LSB
    { .reg = 0x380a, .val = 0x01 },  // V output size MSB (480)
    { .reg = 0x380b, .val = 0xe0 },  // V output size LSB
    { .reg = 0x380c, .val = 0x03 },  // HTS MSB
    { .reg = 0x380d, .val = 0x20 },  // HTS LSB (800)
    { .reg = 0x380e, .val = 0x02 },  // VTS MSB
    { .reg = 0x380f, .val = 0x08 },  // VTS LSB (520)
    { .reg = 0x3810, .val = 0x02 },  // H offset MSB
    { .reg = 0x3811, .val = 0x80 },  // H offset LSB
    { .reg = 0x3812, .val = 0x01 },  // V offset MSB
    { .reg = 0x3813, .val = 0x2c },  // V offset LSB
    { .reg = 0x3820, .val = 0x00 },  // No flip
    { .reg = 0x3821, .val = 0x00 },  // No mirror
    { .reg = 0x4837, .val = 0x20 },  // MIPI timing
    { .reg = 0x0100, .val = 0x01 },  // Start streaming
};

static const esp_cam_sensor_isp_info_t ov02c10_640x480_isp_info = {
    .isp_v1_info = {
        .version = SENSOR_ISP_INFO_VERSION_DEFAULT,
        .pclk = 12480000,     // HTS × VTS × FPS = 800 × 520 × 30
        .hts = 800,
        .vts = 520,
        .exp_def = 0x200,
        .gain_def = 0x100,
        .bayer_type = ESP_CAM_SENSOR_BAYER_GBRG,  // Match OV02C10 driver
    }
};

static const esp_cam_sensor_format_t ov02c10_format_640x480_raw10_30fps = {
    .name = "MIPI_1lane_24Minput_RAW10_640x480_30fps",
    .format = ESP_CAM_SENSOR_PIXFORMAT_RAW10,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .xclk = 24000000,
    .width = 640,
    .height = 480,
    .regs = ov02c10_640x480_raw10_30fps,
    .regs_size = ARRAY_SIZE(ov02c10_640x480_raw10_30fps),
    .fps = 30,
    .isp_info = &ov02c10_640x480_isp_info,
    .mipi_info = {
        .mipi_clk = 250000000,
        .lane_num = 1,
        .line_sync_en = false,
    },
    .reserved = NULL,
};

// ============================================================================
// Configuration 2 : 800x600 (SVGA) @ 30fps RAW10
// ============================================================================
// Centered crop from 1920x1080 sensor area
// Crop calculation: startX=(1920-800)/2=560, startY=(1080-600)/2=240

static const ov02c10_reginfo_t ov02c10_800x600_raw10_30fps[] = {
    { .reg = 0x0103, .val = 0x01 },  // Software reset
    { .reg = 0x0100, .val = 0x00 },  // Standby
    { .reg = 0x0302, .val = 0x32 },  // PLL multiplier
    { .reg = 0x030e, .val = 0x02 },  // PLL divider
    { .reg = 0x3808, .val = 0x03 },  // H output size MSB (800)
    { .reg = 0x3809, .val = 0x20 },  // H output size LSB
    { .reg = 0x380a, .val = 0x02 },  // V output size MSB (600)
    { .reg = 0x380b, .val = 0x58 },  // V output size LSB
    { .reg = 0x380c, .val = 0x04 },  // HTS MSB
    { .reg = 0x380d, .val = 0x00 },  // HTS LSB (1024)
    { .reg = 0x380e, .val = 0x02 },  // VTS MSB
    { .reg = 0x380f, .val = 0x70 },  // VTS LSB (624)
    { .reg = 0x3810, .val = 0x02 },  // H offset MSB
    { .reg = 0x3811, .val = 0x30 },  // H offset LSB
    { .reg = 0x3812, .val = 0x00 },  // V offset MSB
    { .reg = 0x3813, .val = 0xf0 },  // V offset LSB
    { .reg = 0x3820, .val = 0x00 },  // No flip
    { .reg = 0x3821, .val = 0x00 },  // No mirror
    { .reg = 0x4837, .val = 0x1c },  // MIPI timing
    { .reg = 0x0100, .val = 0x01 },  // Start streaming
};

static const esp_cam_sensor_isp_info_t ov02c10_800x600_isp_info = {
    .isp_v1_info = {
        .version = SENSOR_ISP_INFO_VERSION_DEFAULT,
        .pclk = 19161600,     // HTS × VTS × FPS = 1024 × 624 × 30
        .hts = 1024,
        .vts = 624,
        .exp_def = 0x250,
        .gain_def = 0x100,
        .bayer_type = ESP_CAM_SENSOR_BAYER_GBRG,  // Match OV02C10 driver
    }
};

static const esp_cam_sensor_format_t ov02c10_format_800x600_raw10_30fps = {
    .name = "MIPI_1lane_24Minput_RAW10_800x600_30fps",
    .format = ESP_CAM_SENSOR_PIXFORMAT_RAW10,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .xclk = 24000000,
    .width = 800,
    .height = 600,
    .regs = ov02c10_800x600_raw10_30fps,
    .regs_size = ARRAY_SIZE(ov02c10_800x600_raw10_30fps),
    .fps = 30,
    .isp_info = &ov02c10_800x600_isp_info,
    .mipi_info = {
        .mipi_clk = 300000000,
        .lane_num = 1,
        .line_sync_en = false,
    },
    .reserved = NULL,
};

#ifdef __cplusplus
}
#endif
