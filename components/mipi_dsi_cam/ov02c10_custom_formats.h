/*
 * OV02C10 Custom Format Configurations
 * Support for non-standard resolutions: 800x480 and 1280x800
 */

#pragma once

#include <stdint.h>
#include "esp_cam_sensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Type pour registre OV02C10 (compatible avec SC2336 structure)
typedef struct {
    uint16_t addr;
    uint8_t val;
} ov02c10_reginfo_t;

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// ============================================================================
// Configuration 1 : 1280x800 @ 30fps RAW10
// ============================================================================
// Note: Ces registres sont des TEMPLATES basés sur les formats standard OV02C10
// Ils devront être ajustés selon le datasheet OV02C10 réel

static const ov02c10_reginfo_t ov02c10_1280x800_raw10_30fps[] = {
    // Software reset
    {0x0103, 0x01},
    {0x0100, 0x00},  // Standby

    // PLL Configuration (basé sur 24MHz XCLK)
    {0x0302, 0x32},  // PLL multiplier
    {0x030e, 0x02},  // PLL divider

    // Output size: 1280x800
    {0x3808, 0x05},  // H output size MSB (0x0500 = 1280)
    {0x3809, 0x00},  // H output size LSB
    {0x380a, 0x03},  // V output size MSB (0x0320 = 800)
    {0x380b, 0x20},  // V output size LSB

    // Timing configuration
    {0x380c, 0x05},  // HTS (Horizontal Total Size) MSB
    {0x380d, 0xdc},  // HTS LSB (1500 pixels)
    {0x380e, 0x03},  // VTS (Vertical Total Size) MSB
    {0x380f, 0x52},  // VTS LSB (850 lines)

    // Offset (crop from 1920x1080 to 1280x800)
    {0x3810, 0x01},  // H offset MSB ((1920-1280)/2 = 320)
    {0x3811, 0x40},  // H offset LSB
    {0x3812, 0x00},  // V offset MSB ((1080-800)/2 = 140)
    {0x3813, 0x8c},  // V offset LSB

    // Format: RAW10
    {0x3820, 0x00},  // No flip
    {0x3821, 0x00},  // No mirror

    // MIPI configuration
    {0x4837, 0x14},  // MIPI global timing

    // Start streaming
    {0x0100, 0x01},
};

static const esp_cam_sensor_isp_info_t ov02c10_1280x800_isp_info = {
    .isp_v_blanking_lines = 16,
    .ae_enable = true,
    .ae_gain_range = {1.0, 16.0},
    .ae_exposure_range = {16, 1000},
    .awb_enable = true,
};

static const esp_cam_sensor_format_t ov02c10_format_1280x800_raw10_30fps = {
    .name = "MIPI_2lane_24Minput_RAW10_1280x800_30fps",
    .format = ESP_CAM_SENSOR_PIXFORMAT_RAW10,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .xclk = 24000000,
    .width = 1280,
    .height = 800,
    .regs = ov02c10_1280x800_raw10_30fps,
    .regs_size = ARRAY_SIZE(ov02c10_1280x800_raw10_30fps),
    .fps = 30,
    .isp_info = &ov02c10_1280x800_isp_info,
    .mipi_info = {
        .mipi_clk = 400000000,  // 400MHz MIPI clock
        .lane_num = 2,
        .line_sync_en = false,
    },
    .reserved = NULL,
};

// ============================================================================
// Configuration 2 : 800x480 @ 30fps RAW10
// ============================================================================

static const ov02c10_reginfo_t ov02c10_800x480_raw10_30fps[] = {
    // Software reset
    {0x0103, 0x01},
    {0x0100, 0x00},  // Standby

    // PLL Configuration (basé sur 24MHz XCLK)
    {0x0302, 0x32},  // PLL multiplier
    {0x030e, 0x02},  // PLL divider

    // Output size: 800x480
    {0x3808, 0x03},  // H output size MSB (0x0320 = 800)
    {0x3809, 0x20},  // H output size LSB
    {0x380a, 0x01},  // V output size MSB (0x01E0 = 480)
    {0x380b, 0xe0},  // V output size LSB

    // Timing configuration
    {0x380c, 0x04},  // HTS (Horizontal Total Size) MSB
    {0x380d, 0x1a},  // HTS LSB (1050 pixels)
    {0x380e, 0x02},  // VTS (Vertical Total Size) MSB
    {0x380f, 0x0e},  // VTS LSB (526 lines)

    // Offset (crop from 1920x1080 to 800x480)
    {0x3810, 0x02},  // H offset MSB ((1920-800)/2 = 560)
    {0x3811, 0x30},  // H offset LSB
    {0x3812, 0x01},  // V offset MSB ((1080-480)/2 = 300)
    {0x3813, 0x2c},  // V offset LSB

    // Format: RAW10
    {0x3820, 0x00},  // No flip
    {0x3821, 0x00},  // No mirror

    // MIPI configuration
    {0x4837, 0x1c},  // MIPI global timing (slower for 800x480)

    // Start streaming
    {0x0100, 0x01},
};

static const esp_cam_sensor_isp_info_t ov02c10_800x480_isp_info = {
    .isp_v_blanking_lines = 16,
    .ae_enable = true,
    .ae_gain_range = {1.0, 16.0},
    .ae_exposure_range = {16, 1000},
    .awb_enable = true,
};

static const esp_cam_sensor_format_t ov02c10_format_800x480_raw10_30fps = {
    .name = "MIPI_2lane_24Minput_RAW10_800x480_30fps",
    .format = ESP_CAM_SENSOR_PIXFORMAT_RAW10,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .xclk = 24000000,
    .width = 800,
    .height = 480,
    .regs = ov02c10_800x480_raw10_30fps,
    .regs_size = ARRAY_SIZE(ov02c10_800x480_raw10_30fps),
    .fps = 30,
    .isp_info = &ov02c10_800x480_isp_info,
    .mipi_info = {
        .mipi_clk = 300000000,  // 300MHz MIPI clock (réduit pour 800x480)
        .lane_num = 2,
        .line_sync_en = false,
    },
    .reserved = NULL,
};

#ifdef __cplusplus
}
#endif
