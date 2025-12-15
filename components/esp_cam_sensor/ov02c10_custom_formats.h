/*
 * OV02C10 Custom Format Configurations
 * Support for VGA and SVGA resolutions: 640x480 and 800x600
 */

#pragma once

#include <stdint.h>
#include "esp_cam_sensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Type pour registre OV02C10
typedef struct {
    uint16_t addr;
    uint8_t val;
} ov02c10_reginfo_t;

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

// ============================================================================
// Configuration 1 : 640x480 (VGA) @ 30fps RAW10
// ============================================================================
// Centered crop from 1920x1080 sensor area
// Crop calculation: startX=(1920-640)/2=640, startY=(1080-480)/2=300

static const ov02c10_reginfo_t ov02c10_640x480_raw10_30fps[] = {
    // Software reset
    {0x0103, 0x01},
    {0x0100, 0x00},  // Standby

    // PLL Configuration (24MHz XCLK → 48MHz PCLK)
    {0x0302, 0x32},  // PLL multiplier
    {0x030e, 0x02},  // PLL divider

    // Output size: 640x480
    {0x3808, 0x02},  // H output size MSB (0x0280 = 640)
    {0x3809, 0x80},  // H output size LSB
    {0x380a, 0x01},  // V output size MSB (0x01E0 = 480)
    {0x380b, 0xe0},  // V output size LSB

    // Timing configuration for 30fps
    {0x380c, 0x03},  // HTS (Horizontal Total Size) MSB
    {0x380d, 0x20},  // HTS LSB (800 pixels)
    {0x380e, 0x02},  // VTS (Vertical Total Size) MSB
    {0x380f, 0x08},  // VTS LSB (520 lines)

    // Centered crop offset
    {0x3810, 0x02},  // H offset MSB (640)
    {0x3811, 0x80},  // H offset LSB
    {0x3812, 0x01},  // V offset MSB (300)
    {0x3813, 0x2c},  // V offset LSB

    // Format: RAW10
    {0x3820, 0x00},  // No flip
    {0x3821, 0x00},  // No mirror

    // MIPI configuration
    {0x4837, 0x20},  // MIPI global timing

    // Start streaming
    {0x0100, 0x01},
};

static const esp_cam_sensor_isp_info_t ov02c10_640x480_isp_info = {
    .isp_v1_info = {
        .version = SENSOR_ISP_INFO_VERSION_DEFAULT,
        .pclk = 12480000,     // HTS × VTS × FPS = 800 × 520 × 30
        .hts = 800,           // Horizontal Total Size
        .vts = 520,           // Vertical Total Size
        .exp_def = 0x200,     // Default exposure
        .gain_def = 0x100,    // Default gain (1x)
        .bayer_type = ESP_CAM_SENSOR_BAYER_BGGR,
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
        .mipi_clk = 250000000,  // 250MHz MIPI clock
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
    // Software reset
    {0x0103, 0x01},
    {0x0100, 0x00},  // Standby

    // PLL Configuration (24MHz XCLK)
    {0x0302, 0x32},  // PLL multiplier
    {0x030e, 0x02},  // PLL divider

    // Output size: 800x600
    {0x3808, 0x03},  // H output size MSB (0x0320 = 800)
    {0x3809, 0x20},  // H output size LSB
    {0x380a, 0x02},  // V output size MSB (0x0258 = 600)
    {0x380b, 0x58},  // V output size LSB

    // Timing configuration for 30fps
    {0x380c, 0x04},  // HTS (Horizontal Total Size) MSB
    {0x380d, 0x00},  // HTS LSB (1024 pixels)
    {0x380e, 0x02},  // VTS (Vertical Total Size) MSB
    {0x380f, 0x70},  // VTS LSB (624 lines)

    // Centered crop offset
    {0x3810, 0x02},  // H offset MSB (560)
    {0x3811, 0x30},  // H offset LSB
    {0x3812, 0x00},  // V offset MSB (240)
    {0x3813, 0xf0},  // V offset LSB

    // Format: RAW10
    {0x3820, 0x00},  // No flip
    {0x3821, 0x00},  // No mirror

    // MIPI configuration
    {0x4837, 0x1c},  // MIPI global timing

    // Start streaming
    {0x0100, 0x01},
};

static const esp_cam_sensor_isp_info_t ov02c10_800x600_isp_info = {
    .isp_v1_info = {
        .version = SENSOR_ISP_INFO_VERSION_DEFAULT,
        .pclk = 19161600,     // HTS × VTS × FPS = 1024 × 624 × 30
        .hts = 1024,          // Horizontal Total Size
        .vts = 624,           // Vertical Total Size
        .exp_def = 0x250,     // Default exposure
        .gain_def = 0x100,    // Default gain (1x)
        .bayer_type = ESP_CAM_SENSOR_BAYER_BGGR,
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
        .mipi_clk = 300000000,  // 300MHz MIPI clock
        .lane_num = 1,
        .line_sync_en = false,
    },
    .reserved = NULL,
};

#ifdef __cplusplus
}
#endif
