/*
 * OV5647 Custom Format Configurations
 * Support for non-standard resolutions: VGA (640x480) and 1024x600
 *
 * These formats are optimized for small LCD displays commonly used
 * with M5Stack and similar ESP32-P4 development boards.
 */

#pragma once

#include <stdint.h>
#include "esp_cam_sensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Type pour registre OV5647
typedef struct {
    uint16_t addr;
    uint8_t val;
} ov5647_reginfo_t;

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

// OV5647 register end marker
#define OV5647_REG_END     0xffff
#define OV5647_REG_DELAY   0xfffe

// Bit manipulation macro
#ifndef BIT
#define BIT(nr) (1UL << (nr))
#endif

// Clock rates for custom formats
#define OV5647_IDI_CLOCK_RATE_640x480_30FPS        (48000000ULL)
#define OV5647_MIPI_CSI_LINE_RATE_640x480_30FPS    (OV5647_IDI_CLOCK_RATE_640x480_30FPS * 4)
#define OV5647_IDI_CLOCK_RATE_800x640_50FPS        (100000000ULL)  // From testov5647 working config
#define OV5647_MIPI_CSI_LINE_RATE_800x640_50FPS    (OV5647_IDI_CLOCK_RATE_800x640_50FPS * 4)
#define OV5647_IDI_CLOCK_RATE_1024x600_30FPS       (72000000ULL)
#define OV5647_MIPI_CSI_LINE_RATE_1024x600_30FPS   (OV5647_IDI_CLOCK_RATE_1024x600_30FPS * 4)

// OV5647 bit mode (8-bit RAW8)
#define OV5647_8BIT_MODE   0x18

// ============================================================================
// Configuration 1 : VGA 640x480 @ 30fps RAW8
// ============================================================================
// VGA is a standard resolution that works well with OV5647 and requires
// minimal cropping from the sensor's native 2592×1944 active area.

static const ov5647_reginfo_t ov5647_input_24M_MIPI_2lane_raw8_640x480_30fps[] = {
    // Software reset
    {0x0103, 0x01},
    {OV5647_REG_DELAY, 0x0a},
    {0x0100, 0x00},  // Standby

    // RAW8 mode configuration
    {0x3034, OV5647_8BIT_MODE},  // Set RAW8 format
    {0x3035, 0x21},  // System clock divider (slower for 30fps)
    {0x3036, ((OV5647_IDI_CLOCK_RATE_640x480_30FPS * 8 * 4) / 25000000)},  // PLL multiplier
    {0x303c, 0x11},  // PLLS control
    {0x3106, 0xf5},
    {0x3821, 0x03},  // Horizontal binning + mirror (fix: sensor appears right-shifted)
    {0x3820, 0x41},  // Vertical binning
    {0x3827, 0xec},
    {0x370c, 0x0f},
    {0x3612, 0x59},
    {0x3618, 0x00},
    {0x5000, 0xff},  // Enable all ISP blocks

    // LSC (Lens Shading Correction)
    {0x583e, 0xf0},  // LSC max gain
    {0x583f, 0x20},  // LSC min gain

    {0x5002, 0x41},
    {0x5003, 0x08},
    {0x5a00, 0x08},
    {0x3000, 0x00},
    {0x3001, 0x00},
    {0x3002, 0x00},
    {0x3016, 0x08},
    {0x3017, 0xe0},
    {0x3018, 0x44},
    {0x301c, 0xf8},
    {0x301d, 0xf0},
    {0x3a18, 0x00},
    {0x3a19, 0xf8},
    {0x3c01, 0x80},
    {0x3c00, 0x40},
    {0x3b07, 0x0c},

    // Timing configuration
    // HTS (Horizontal Total Size) in pixels
    {0x380c, (1896 >> 8) & 0x1F},
    {0x380d, 1896 & 0xFF},
    // VTS (Vertical Total Size) in lines
    {0x380e, (1080 >> 8) & 0xFF},
    {0x380f, 1080 & 0xFF},

    // Binning configuration for VGA
    {0x3814, 0x31},  // Horizontal subsample (4x binning)
    {0x3815, 0x31},  // Vertical subsample (4x binning)
    {0x3708, 0x64},
    {0x3709, 0x52},

    // Crop window (center crop from 2592x1944)
    // X start: (2592 - 640*4) / 2 = 0 (use full width with binning)
    {0x3800, (0 >> 8) & 0x0F},   // X address start high
    {0x3801, 0 & 0xFF},          // X address start low
    // Y start: (1944 - 480*4) / 2 = 12
    {0x3802, (12 >> 8) & 0x07},  // Y address start high
    {0x3803, 12 & 0xFF},         // Y address start low
    // X end: 2592 - 1
    {0x3804, ((2592 - 1) >> 8) & 0x0F},  // X address end high
    {0x3805, (2592 - 1) & 0xFF},         // X address end low
    // Y end: 1944 - 1
    {0x3806, ((1944 - 1) >> 8) & 0x07},  // Y address end high
    {0x3807, (1944 - 1) & 0xFF},         // Y address end low

    // Output size: 640x480
    {0x3808, (640 >> 8) & 0x0F},  // Output horizontal width high
    {0x3809, 640 & 0xFF},         // Output horizontal width low
    {0x380a, (480 >> 8) & 0x7F},  // Output vertical height high
    {0x380b, 480 & 0xFF},         // Output vertical height low

    // Timing offset (center the image properly)
    // After 4x binning: 2592/4=648 pixels, want 640 → offset (648-640)/2 = 4
    {0x3810, (4 >> 8) & 0x0F},   // Timing horizontal offset high (centered)
    {0x3811, 4 & 0xFF},          // Timing horizontal offset low
    {0x3812, (3 >> 8) & 0x07},   // Timing vertical offset high (centered)
    {0x3813, 3 & 0xFF},          // Timing vertical offset low

    // Analog settings
    {0x3630, 0x2e},
    {0x3632, 0xe2},
    {0x3633, 0x23},
    {0x3634, 0x44},
    {0x3636, 0x06},
    {0x3620, 0x64},
    {0x3621, 0xe0},
    {0x3600, 0x37},
    {0x3704, 0xa0},
    {0x3703, 0x5a},
    {0x3715, 0x78},
    {0x3717, 0x01},
    {0x3731, 0x02},
    {0x370b, 0x60},
    {0x3705, 0x1a},

    // AEC/AGC settings
    {0x3503, 0x00},  // Enable auto exposure and auto gain (0x00 = both auto, 0x03 = both manual)
    {0x3f05, 0x02},
    {0x3f06, 0x10},
    {0x3f01, 0x0a},
    {0x3a08, 0x01},
    {0x3a09, 0x27},
    {0x3a0a, 0x00},
    {0x3a0b, 0xf6},
    {0x3a0d, 0x04},
    {0x3a0e, 0x03},
    {0x3a0f, 0x58},
    {0x3a10, 0x50},
    {0x3a1b, 0x58},
    {0x3a1e, 0x50},
    {0x3a11, 0x60},
    {0x3a1f, 0x28},

    // BLC (Black Level Calibration)
    {0x4001, 0x02},
    {0x4004, 0x02},
    {0x4000, 0x09},
    {0x4837, 0x24},  // MIPI pclk period
    {0x4050, 0x6e},
    {0x4051, 0x8f},

    // MIPI configuration
    {0x4800, BIT(5)},  // MIPI clock lane gate enable

    // AWB settings
    {0x5180, 0xff},
    {0x5181, 0xf2},
    {0x5182, 0x00},
    {0x5183, 0x14},
    {0x5184, 0x25},
    {0x5185, 0x24},
    {0x5186, 0x09},
    {0x5187, 0x09},
    {0x5188, 0x0a},
    {0x5189, 0x75},
    {0x518a, 0x52},
    {0x518b, 0xea},
    {0x518c, 0xa8},
    {0x518d, 0x42},
    {0x518e, 0x38},
    {0x518f, 0x56},
    {0x5190, 0x42},
    {0x5191, 0xf8},
    {0x5192, 0x04},
    {0x5193, 0x70},
    {0x5194, 0xf0},
    {0x5195, 0xf0},
    {0x5196, 0x03},
    {0x5197, 0x01},
    {0x5198, 0x04},
    {0x5199, 0x12},
    {0x519a, 0x04},
    {0x519b, 0x00},
    {0x519c, 0x06},
    {0x519d, 0x82},
    {0x519e, 0x38},

    // Start streaming
    {0x0100, 0x01},
    {OV5647_REG_END, 0x00},
};

static const esp_cam_sensor_isp_info_t ov5647_640x480_isp_info = {
    .isp_v1_info = {
        .version = SENSOR_ISP_INFO_VERSION_DEFAULT,
        .pclk = 32432000,     // HTS × VTS × FPS = 1896 × 1080 × 30 / 2
        .hts = 1896,          // Horizontal Total Size
        .vts = 1080,          // Vertical Total Size
        .exp_def = 0x300,     // 768 - restored to original value, let AEC handle it
        .gain_def = 0x100,    // Default gain (1x)
        .bayer_type = ESP_CAM_SENSOR_BAYER_GBRG,  // GBRG (BGGR mirrored horizontally)
    }
};

static const esp_cam_sensor_format_t ov5647_format_640x480_raw8_30fps = {
    .name = "MIPI_2lane_24Minput_RAW8_640x480_30fps",
    .format = ESP_CAM_SENSOR_PIXFORMAT_RAW8,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .xclk = 24000000,
    .width = 640,
    .height = 480,
    .regs = ov5647_input_24M_MIPI_2lane_raw8_640x480_30fps,
    .regs_size = ARRAY_SIZE(ov5647_input_24M_MIPI_2lane_raw8_640x480_30fps),
    .fps = 30,
    .isp_info = &ov5647_640x480_isp_info,
    .mipi_info = {
        .mipi_clk = OV5647_MIPI_CSI_LINE_RATE_640x480_30FPS,
        .lane_num = 2,
        .line_sync_en = false,
    },
    .reserved = NULL,
};

// ============================================================================
// Configuration 2 : 1024x600 @ 30fps RAW8
// ============================================================================
// 1024x600 is a common resolution for 7" LCD displays
// Requires center cropping from OV5647's native resolution

static const ov5647_reginfo_t ov5647_input_24M_MIPI_2lane_raw8_1024x600_30fps[] = {
    // Software reset
    {0x0103, 0x01},
    {OV5647_REG_DELAY, 0x0a},
    {0x0100, 0x00},  // Standby

    // RAW8 mode configuration
    {0x3034, OV5647_8BIT_MODE},  // Set RAW8 format
    {0x3035, 0x21},  // System clock divider
    {0x3036, ((OV5647_IDI_CLOCK_RATE_1024x600_30FPS * 8 * 4) / 25000000)},  // PLL multiplier
    {0x303c, 0x11},
    {0x3106, 0xf5},
    {0x3821, 0x03},  // Horizontal binning (2x) + mirror (fix: sensor appears right-shifted)
    {0x3820, 0x41},  // Vertical binning (2x)
    {0x3827, 0xec},
    {0x370c, 0x0f},
    {0x3612, 0x59},
    {0x3618, 0x00},
    {0x5000, 0xff},

    // LSC settings
    {0x583e, 0xf0},
    {0x583f, 0x20},

    {0x5002, 0x41},
    {0x5003, 0x08},
    {0x5a00, 0x08},
    {0x3000, 0x00},
    {0x3001, 0x00},
    {0x3002, 0x00},
    {0x3016, 0x08},
    {0x3017, 0xe0},
    {0x3018, 0x44},
    {0x301c, 0xf8},
    {0x301d, 0xf0},
    {0x3a18, 0x00},
    {0x3a19, 0xf8},
    {0x3c01, 0x80},
    {0x3c00, 0x40},
    {0x3b07, 0x0c},

    // Timing configuration
    // HTS in pixels
    {0x380c, (2416 >> 8) & 0x1F},
    {0x380d, 2416 & 0xFF},
    // VTS in lines
    {0x380e, (1300 >> 8) & 0xFF},
    {0x380f, 1300 & 0xFF},

    // Binning (2x2 for 1024x600)
    {0x3814, 0x11},  // Horizontal subsample (2x)
    {0x3815, 0x11},  // Vertical subsample (2x)
    {0x3708, 0x64},
    {0x3709, 0x52},

    // Crop window (center crop)
    // X start: (2592 - 1024*2) / 2 = 272
    {0x3800, (272 >> 8) & 0x0F},
    {0x3801, 272 & 0xFF},
    // Y start: (1944 - 600*2) / 2 = 372
    {0x3802, (372 >> 8) & 0x07},
    {0x3803, 372 & 0xFF},
    // X end: 272 + 1024*2 - 1 = 2319
    {0x3804, ((2319) >> 8) & 0x0F},
    {0x3805, (2319) & 0xFF},
    // Y end: 372 + 600*2 - 1 = 1571
    {0x3806, ((1571) >> 8) & 0x07},
    {0x3807, (1571) & 0xFF},

    // Output size: 1024x600
    {0x3808, (1024 >> 8) & 0x0F},
    {0x3809, 1024 & 0xFF},
    {0x380a, (600 >> 8) & 0x7F},
    {0x380b, 600 & 0xFF},

    // Timing offset (center the image properly)
    // After 2x binning: 2048/2=1024 pixels (already correct)
    {0x3810, (0 >> 8) & 0x0F},   // Timing horizontal offset high (centered)
    {0x3811, 0 & 0xFF},          // Timing horizontal offset low
    {0x3812, (0 >> 8) & 0x07},   // Timing vertical offset high (centered)
    {0x3813, 0 & 0xFF},          // Timing vertical offset low

    // Analog settings
    {0x3630, 0x2e},
    {0x3632, 0xe2},
    {0x3633, 0x23},
    {0x3634, 0x44},
    {0x3636, 0x06},
    {0x3620, 0x64},
    {0x3621, 0xe0},
    {0x3600, 0x37},
    {0x3704, 0xa0},
    {0x3703, 0x5a},
    {0x3715, 0x78},
    {0x3717, 0x01},
    {0x3731, 0x02},
    {0x370b, 0x60},
    {0x3705, 0x1a},

    // AEC/AGC settings
    {0x3503, 0x00},  // Enable auto exposure and auto gain (0x00 = both auto, 0x03 = both manual)
    {0x3f05, 0x02},
    {0x3f06, 0x10},
    {0x3f01, 0x0a},
    {0x3a08, 0x01},
    {0x3a09, 0x4b},
    {0x3a0a, 0x01},
    {0x3a0b, 0x13},
    {0x3a0d, 0x04},
    {0x3a0e, 0x03},
    {0x3a0f, 0x58},
    {0x3a10, 0x50},
    {0x3a1b, 0x58},
    {0x3a1e, 0x50},
    {0x3a11, 0x60},
    {0x3a1f, 0x28},

    // BLC settings
    {0x4001, 0x02},
    {0x4004, 0x04},
    {0x4000, 0x09},
    {0x4837, 0x19},  // MIPI pclk period
    {0x4050, 0x6e},
    {0x4051, 0x8f},

    // MIPI configuration
    {0x4800, BIT(5)},

    // AWB settings
    {0x5180, 0xff},
    {0x5181, 0xf2},
    {0x5182, 0x00},
    {0x5183, 0x14},
    {0x5184, 0x25},
    {0x5185, 0x24},
    {0x5186, 0x09},
    {0x5187, 0x09},
    {0x5188, 0x0a},
    {0x5189, 0x75},
    {0x518a, 0x52},
    {0x518b, 0xea},
    {0x518c, 0xa8},
    {0x518d, 0x42},
    {0x518e, 0x38},
    {0x518f, 0x56},
    {0x5190, 0x42},
    {0x5191, 0xf8},
    {0x5192, 0x04},
    {0x5193, 0x70},
    {0x5194, 0xf0},
    {0x5195, 0xf0},
    {0x5196, 0x03},
    {0x5197, 0x01},
    {0x5198, 0x04},
    {0x5199, 0x12},
    {0x519a, 0x04},
    {0x519b, 0x00},
    {0x519c, 0x06},
    {0x519d, 0x82},
    {0x519e, 0x38},

    // Start streaming
    {0x0100, 0x01},
    {OV5647_REG_END, 0x00},
};

static const esp_cam_sensor_isp_info_t ov5647_1024x600_isp_info = {
    .isp_v1_info = {
        .version = SENSOR_ISP_INFO_VERSION_DEFAULT,
        .pclk = 94224000,     // HTS × VTS × FPS = 2416 × 1300 × 30
        .hts = 2416,          // Horizontal Total Size
        .vts = 1300,          // Vertical Total Size
        .exp_def = 0x500,     // 1280 - restored to original value, let AEC handle it
        .gain_def = 0x100,    // Default gain (1x)
        .bayer_type = ESP_CAM_SENSOR_BAYER_GBRG,  // GBRG (BGGR mirrored horizontally)
    }
};

static const esp_cam_sensor_format_t ov5647_format_1024x600_raw8_30fps = {
    .name = "MIPI_2lane_24Minput_RAW8_1024x600_30fps",
    .format = ESP_CAM_SENSOR_PIXFORMAT_RAW8,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .xclk = 24000000,
    .width = 1024,
    .height = 600,
    .regs = ov5647_input_24M_MIPI_2lane_raw8_1024x600_30fps,
    .regs_size = ARRAY_SIZE(ov5647_input_24M_MIPI_2lane_raw8_1024x600_30fps),
    .fps = 30,
    .isp_info = &ov5647_1024x600_isp_info,
    .mipi_info = {
        .mipi_clk = OV5647_MIPI_CSI_LINE_RATE_1024x600_30FPS,
        .lane_num = 2,
        .line_sync_en = false,
    },
    .reserved = NULL,
};

// ============================================================================
// Configuration 3 : 800x600 @ 30fps RAW8 (for 1024x600 displays)
// ============================================================================
// Optimized for 1024x600 displays with centered camera view
// Camera outputs 800x600, centered on screen at position (112, 0)

static const ov5647_reginfo_t ov5647_input_24M_MIPI_2lane_raw8_800x600_30fps[] = {
    // RAW8 mode configuration (based on 800x640 working config)
    {0x3034, OV5647_8BIT_MODE},  // 8-bit RAW8 format
    {0x3035, 0x21},  // System clock divider (30 fps vs 50fps for 800x640)
    {0x3036, ((OV5647_IDI_CLOCK_RATE_640x480_30FPS * 8 * 4) / 25000000)},  // PLL multiplier
    {0x303c, 0x11},  // PLLS control
    {0x3106, 0xf5},
    {0x3821, 0x03},  // Horizontal binning + mirror
    {0x3820, 0x41},  // Vertical binning
    {0x3827, 0xec},
    {0x370c, 0x0f},
    {0x3612, 0x59},
    {0x3618, 0x00},
    {0x5000, 0xff},  // Enable all ISP blocks

    // LSC (Lens Shading Correction)
    {0x583e, 0xf0},  // LSC max gain
    {0x583f, 0x20},  // LSC min gain

    {0x5002, 0x41},
    {0x5003, 0x08},
    {0x5a00, 0x08},
    {0x3000, 0x00},
    {0x3001, 0x00},
    {0x3002, 0x00},
    {0x3016, 0x08},
    {0x3017, 0xe0},
    {0x3018, 0x44},
    {0x301c, 0xf8},
    {0x301d, 0xf0},
    {0x3a18, 0x00},
    {0x3a19, 0xf8},
    {0x3c01, 0x80},
    {0x3c00, 0x40},
    {0x3b07, 0x0c},

    // Timing configuration for 800x600 @ 30fps
    // HTS (Horizontal Total Size) = 1896 pixels (same as 800x640)
    {0x380c, (1896 >> 8) & 0x1F},
    {0x380d, 1896 & 0xFF},
    // VTS (Vertical Total Size) = 920 lines (adapted from 984 for 600 vs 640)
    {0x380e, (920 >> 8) & 0xFF},
    {0x380f, 920 & 0xFF},

    // Binning configuration (same as 800x640)
    {0x3814, 0x31},  // Horizontal subsample
    {0x3815, 0x31},  // Vertical subsample
    {0x3708, 0x64},
    {0x3709, 0x52},

    // Crop window (adapted from 800x640: keep X same, adjust Y for 4:3 ratio)
    // X: same as 800x640 (500 to 2623 = 2124 pixels width)
    {0x3800, (500 >> 8) & 0x0F},   // X address start high
    {0x3801, 500 & 0xFF},          // X address start low
    // Y: centered crop for 800x600 (4:3 ratio)
    // Crop height: 2124 * 3/4 = 1593, centered: (1954-1593)/2 = 180
    {0x3802, (180 >> 8) & 0x07},   // Y address start high
    {0x3803, 180 & 0xFF},          // Y address start low
    {0x3804, ((2624 - 1) >> 8) & 0x0F},  // X address end high (same as 800x640)
    {0x3805, (2624 - 1) & 0xFF},         // X address end low
    // Y end: 180 + 1593 - 1 = 1772
    {0x3806, ((1772 - 1) >> 8) & 0x07},  // Y address end high
    {0x3807, (1772 - 1) & 0xFF},         // Y address end low

    // Output size: 800x600
    {0x3808, (800 >> 8) & 0x0F},  // Output horizontal width high
    {0x3809, 800 & 0xFF},         // Output horizontal width low
    {0x380a, (600 >> 8) & 0x7F},  // Output vertical height high
    {0x380b, 600 & 0xFF},         // Output vertical height low

    // Timing offset (same as 800x640)
    {0x3810, (8 >> 8) & 0x0F},   // Timing horizontal offset high
    {0x3811, 8 & 0xFF},          // Timing horizontal offset low
    {0x3812, (0 >> 8) & 0x07},   // Timing vertical offset high
    {0x3813, 0 & 0xFF},          // Timing vertical offset low

    // Analog settings (same as 800x640)
    {0x3630, 0x2e},
    {0x3632, 0xe2},
    {0x3633, 0x23},
    {0x3634, 0x44},
    {0x3636, 0x06},
    {0x3620, 0x64},
    {0x3621, 0xe0},
    {0x3600, 0x37},
    {0x3704, 0xa0},
    {0x3703, 0x5a},
    {0x3715, 0x78},
    {0x3717, 0x01},
    {0x3731, 0x02},
    {0x370b, 0x60},
    {0x3705, 0x1a},

    // AEC/AGC settings (same as 800x640)
    {0x3503, 0x00},  // Enable auto exposure and auto gain
    {0x3f05, 0x02},
    {0x3f06, 0x10},
    {0x3f01, 0x0a},
    {0x3a08, 0x01},
    {0x3a09, 0x27},
    {0x3a0a, 0x00},
    {0x3a0b, 0xf6},
    {0x3a0d, 0x04},
    {0x3a0e, 0x03},
    {0x3a0f, 0x58},
    {0x3a10, 0x50},
    {0x3a1b, 0x58},
    {0x3a1e, 0x50},
    {0x3a11, 0x60},
    {0x3a1f, 0x28},

    // BLC (Black Level Calibration) (same as 800x640)
    {0x4001, 0x02},
    {0x4004, 0x02},
    {0x4000, 0x09},
    {0x4837, 0x24},  // MIPI pclk period
    {0x4050, 0x6e},
    {0x4051, 0x8f},

    // MIPI configuration (same as 800x640)
    {0x4800, BIT(5)},  // MIPI clock lane gate enable

    // AWB settings (same as 800x640)
    {0x5180, 0xff},
    {0x5181, 0xf2},
    {0x5182, 0x00},
    {0x5183, 0x14},
    {0x5184, 0x25},
    {0x5185, 0x24},
    {0x5186, 0x09},
    {0x5187, 0x09},
    {0x5188, 0x0a},
    {0x5189, 0x75},
    {0x518a, 0x52},
    {0x518b, 0xea},
    {0x518c, 0xa8},
    {0x518d, 0x42},
    {0x518e, 0x38},
    {0x518f, 0x56},
    {0x5190, 0x42},
    {0x5191, 0xf8},
    {0x5192, 0x04},
    {0x5193, 0x70},
    {0x5194, 0xf0},
    {0x5195, 0xf0},
    {0x5196, 0x03},
    {0x5197, 0x01},
    {0x5198, 0x04},
    {0x5199, 0x12},
    {0x519a, 0x04},
    {0x519b, 0x00},
    {0x519c, 0x06},
    {0x519d, 0x82},
    {0x519e, 0x38},

    // End marker
    {OV5647_REG_END, 0x00},
};

static const esp_cam_sensor_isp_info_t ov5647_800x600_isp_info = {
    .isp_v1_info = {
        .version = SENSOR_ISP_INFO_VERSION_DEFAULT,
        .pclk = 52344000,     // HTS × VTS × FPS = 1896 × 920 × 30
        .hts = 1896,          // Horizontal Total Size (same as 800x640)
        .vts = 920,           // Vertical Total Size (adapted for 600 lines)
        .exp_def = 0x300,     // Default exposure (same as 800x640)
        .gain_def = 0x100,    // Default gain (1x)
        .bayer_type = ESP_CAM_SENSOR_BAYER_GBRG,  // GBRG (BGGR mirrored horizontally)
    }
};

static const esp_cam_sensor_format_t ov5647_format_800x600_raw8_30fps = {
    .name = "MIPI_2lane_24Minput_RAW8_800x600_30fps",
    .format = ESP_CAM_SENSOR_PIXFORMAT_RAW8,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .xclk = 24000000,
    .width = 800,
    .height = 600,
    .regs = ov5647_input_24M_MIPI_2lane_raw8_800x600_30fps,
    .regs_size = ARRAY_SIZE(ov5647_input_24M_MIPI_2lane_raw8_800x600_30fps),
    .fps = 30,
    .isp_info = &ov5647_800x600_isp_info,
    .mipi_info = {
        .mipi_clk = OV5647_MIPI_CSI_LINE_RATE_640x480_30FPS,
        .lane_num = 2,
        .line_sync_en = false,
    },
    .reserved = NULL,
};

// ============================================================================
// Configuration 4 : 800x640 @ 50fps RAW8 (from testov5647 working config)
// ============================================================================
// This configuration is proven to work well in testov5647 repository with
// good image quality (brightness: 60, contrast: 145, saturation: 135).
// Optimized for 50 FPS with 100MHz clock rate.

static const ov5647_reginfo_t ov5647_input_24M_MIPI_2lane_raw8_800x640_50fps[] = {
    // RAW8 mode configuration
    {0x3034, OV5647_8BIT_MODE},  // 8-bit RAW8 format
    {0x3035, 0x41},  // System clock divider
    {0x3036, ((OV5647_IDI_CLOCK_RATE_800x640_50FPS * 8 * 4) / 25000000)},  // PLL multiplier for 100MHz
    {0x303c, 0x11},  // PLLS control
    {0x3106, 0xf5},
    {0x3821, 0x03},  // Horizontal binning + mirror
    {0x3820, 0x41},  // Vertical binning
    {0x3827, 0xec},
    {0x370c, 0x0f},
    {0x3612, 0x59},
    {0x3618, 0x00},
    {0x5000, 0xff},  // Enable all ISP blocks

    // LSC (Lens Shading Correction)
    {0x583e, 0xf0},  // LSC max gain
    {0x583f, 0x20},  // LSC min gain

    {0x5002, 0x41},
    {0x5003, 0x08},
    {0x5a00, 0x08},
    {0x3000, 0x00},
    {0x3001, 0x00},
    {0x3002, 0x00},
    {0x3016, 0x08},
    {0x3017, 0xe0},
    {0x3018, 0x44},
    {0x301c, 0xf8},
    {0x301d, 0xf0},
    {0x3a18, 0x00},
    {0x3a19, 0xf8},
    {0x3c01, 0x80},
    {0x3c00, 0x40},
    {0x3b07, 0x0c},

    // Timing configuration for 800x640 @ 50fps
    // HTS (Horizontal Total Size) = 1896 pixels
    {0x380c, (1896 >> 8) & 0x1F},
    {0x380d, 1896 & 0xFF},
    // VTS (Vertical Total Size) = 984 lines
    {0x380e, (984 >> 8) & 0xFF},
    {0x380f, 984 & 0xFF},

    // Binning configuration
    {0x3814, 0x31},  // Horizontal subsample
    {0x3815, 0x31},  // Vertical subsample
    {0x3708, 0x64},
    {0x3709, 0x52},

    // Crop window (from testov5647: X start 500, Y start 0, size 2124x1954)
    {0x3800, (500 >> 8) & 0x0F},   // X address start high
    {0x3801, 500 & 0xFF},          // X address start low
    {0x3802, (0 >> 8) & 0x07},     // Y address start high
    {0x3803, 0 & 0xFF},            // Y address start low
    {0x3804, ((2624 - 1) >> 8) & 0x0F},  // X address end high
    {0x3805, (2624 - 1) & 0xFF},         // X address end low
    {0x3806, ((1954 - 1) >> 8) & 0x07},  // Y address end high
    {0x3807, (1954 - 1) & 0xFF},         // Y address end low

    // Output size: 800x640
    {0x3808, (800 >> 8) & 0x0F},  // Output horizontal width high
    {0x3809, 800 & 0xFF},         // Output horizontal width low
    {0x380a, (640 >> 8) & 0x7F},  // Output vertical height high
    {0x380b, 640 & 0xFF},         // Output vertical height low

    // Timing offset
    {0x3810, (8 >> 8) & 0x0F},   // Timing horizontal offset high
    {0x3811, 8 & 0xFF},          // Timing horizontal offset low
    {0x3812, (0 >> 8) & 0x07},   // Timing vertical offset high
    {0x3813, 0 & 0xFF},          // Timing vertical offset low

    // Analog settings
    {0x3630, 0x2e},
    {0x3632, 0xe2},
    {0x3633, 0x23},
    {0x3634, 0x44},
    {0x3636, 0x06},
    {0x3620, 0x64},
    {0x3621, 0xe0},
    {0x3600, 0x37},
    {0x3704, 0xa0},
    {0x3703, 0x5a},
    {0x3715, 0x78},
    {0x3717, 0x01},
    {0x3731, 0x02},
    {0x370b, 0x60},
    {0x3705, 0x1a},

    // AEC/AGC settings (from testov5647)
    {0x3f05, 0x02},
    {0x3f06, 0x10},
    {0x3f01, 0x0a},
    {0x3a08, 0x01},
    {0x3a09, 0x27},
    {0x3a0a, 0x00},
    {0x3a0b, 0xf6},
    {0x3a0d, 0x04},
    {0x3a0e, 0x03},
    {0x3a0f, 0x58},
    {0x3a10, 0x50},
    {0x3a1b, 0x58},
    {0x3a1e, 0x50},
    {0x3a11, 0x60},
    {0x3a1f, 0x28},

    // BLC (Black Level Calibration)
    {0x4001, 0x02},
    {0x4004, 0x02},
    {0x4000, 0x09},
    {0x4837, (1000000000 / (OV5647_IDI_CLOCK_RATE_800x640_50FPS / 4))},  // MIPI pclk period
    {0x4050, 0x6e},
    {0x4051, 0x8f},

    // End marker
    {OV5647_REG_END, 0x00},
};

static const esp_cam_sensor_isp_info_t ov5647_800x640_isp_info = {
    .isp_v1_info = {
        .version = SENSOR_ISP_INFO_VERSION_DEFAULT,
        .pclk = 93312000,     // HTS × VTS × FPS = 1896 × 984 × 50
        .hts = 1896,          // Horizontal Total Size (from testov5647)
        .vts = 984,           // Vertical Total Size (from testov5647)
        .exp_def = 0x300,     // 768 - let AEC handle exposure
        .gain_def = 0x100,    // Default gain (1x)
        .bayer_type = ESP_CAM_SENSOR_BAYER_GBRG,  // GBRG (BGGR mirrored horizontally with 0x3821=0x03)
    }
};

static const esp_cam_sensor_format_t ov5647_format_800x640_raw8_50fps = {
    .name = "MIPI_2lane_24Minput_RAW8_800x640_50fps",
    .format = ESP_CAM_SENSOR_PIXFORMAT_RAW8,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .xclk = 24000000,
    .width = 800,
    .height = 640,
    .regs = ov5647_input_24M_MIPI_2lane_raw8_800x640_50fps,
    .regs_size = ARRAY_SIZE(ov5647_input_24M_MIPI_2lane_raw8_800x640_50fps),
    .fps = 50,
    .isp_info = &ov5647_800x640_isp_info,
    .mipi_info = {
        .mipi_clk = OV5647_MIPI_CSI_LINE_RATE_800x640_50FPS,
        .lane_num = 2,
        .line_sync_en = false,
    },
    .reserved = NULL,
};

#ifdef __cplusplus
}
#endif
