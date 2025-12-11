/*
 * SC202CS Custom Format Configurations
 * Support for 1280x720, 1600x1200 and custom 800x600
 *
 * SC202CS native resolution is 1600×1200.
 * SC202CS does NOT support hardware binning!
 * 800×600 mode uses a CENTERED CROP on the 1600×1200 sensor.
 */

#pragma once

#include <stdint.h>
#include "esp_cam_sensor_types.h"

/* Use official SC202CS type from esp_cam_sensor component */
#include "sc202cs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

/* SC202CS register markers - must match official definitions */
#ifndef SC202CS_REG_END
#define SC202CS_REG_END        0xFFFF
#endif
#ifndef SC202CS_REG_SLEEP_MODE
#define SC202CS_REG_SLEEP_MODE 0x0100
#endif

/* --------------------------------------------------------------------------
 *  Mode 800x600 @ 30fps (CROP centré, PAS de binning)
 *
 *  Le SC202CS n'a PAS de binning hardware !
 *  Ce mode utilise un CROP centré sur le capteur 1600x1200.
 *
 *  Calculs (basés sur le mode 1280x720 fonctionnel):
 *  - Capteur plein : 1600×1200
 *  - Zone de crop centrée pour 800x600 + 8 pixels de blanking:
 *      startX = 396    = 0x018C  (centre - 404)
 *      endX   = 1203   = 0x04B3  (centre + 403)
 *      startY = 296    = 0x0128  (centre - 304)
 *      endY   = 903    = 0x0387  (centre + 303)
 *  - Zone = 808×608, Sortie = 800×600 avec offset 4
 * --------------------------------------------------------------------------*/

static const sc202cs_reginfo_t init_reglist_MIPI_1lane_raw8_800x600_30fps[] = {
    {0x0103, 0x01},          {SC202CS_REG_SLEEP_MODE, 0x00},
    {0x36e9, 0x80},          {0x36ea, 0x06},
    {0x36eb, 0x0a},          {0x36ec, 0x01},
    {0x36ed, 0x18},          {0x36e9, 0x24},
    {0x301f, 0x18},          {0x3031, 0x08},
    {0x3037, 0x00},

    /* ROI centré 808x608 sur capteur 1600x1200 */
    {0x3200, 0x01},          /* x_start MSB = 396 (0x018C) */
    {0x3201, 0x8c},          /* x_start LSB */
    {0x3202, 0x01},          /* y_start MSB = 296 (0x0128) */
    {0x3203, 0x28},          /* y_start LSB */
    {0x3204, 0x04},          /* x_end MSB = 1203 (0x04B3) */
    {0x3205, 0xb3},          /* x_end LSB */
    {0x3206, 0x03},          /* y_end MSB = 903 (0x0387) */
    {0x3207, 0x87},          /* y_end LSB */

    {0x3208, 0x03},          /* output width MSB = 800 (0x0320) */
    {0x3209, 0x20},          /* output width LSB */
    {0x320a, 0x02},          /* output height MSB = 600 (0x0258) */
    {0x320b, 0x58},          /* output height LSB */

    /* Frame timing - MUST set for 30fps (addresses 0x320C-0x320F) */
    /* FPS = pclk / (HTS * VTS) = 72MHz / (1920 * 1250) = 30fps */
    {0x320c, 0x07},          /* HTS MSB = 1920 (0x0780) */
    {0x320d, 0x80},          /* HTS LSB */
    {0x320e, 0x04},          /* VTS MSB = 1250 (0x04E2) */
    {0x320f, 0xe2},          /* VTS LSB */

    {0x3210, 0x00},          /* x offset = 4 (comme mode 1280x720) */
    {0x3211, 0x04},
    {0x3212, 0x00},          /* y offset = 4 */
    {0x3213, 0x04},

    /* Analog/Timing/ISP identiques au mode 1280x720 fonctionnel */
    {0x3301, 0xff},
    {0x3304, 0x68},          {0x3306, 0x40},
    {0x3308, 0x08},          {0x3309, 0xa8},
    {0x330b, 0xd0},          {0x330c, 0x18},
    {0x330d, 0xff},          {0x330e, 0x20},
    {0x331e, 0x59},          {0x331f, 0x99},
    {0x3333, 0x10},          {0x335e, 0x06},
    {0x335f, 0x08},          {0x3364, 0x1f},
    {0x337c, 0x02},          {0x337d, 0x0a},
    {0x338f, 0xa0},          {0x3390, 0x01},
    {0x3391, 0x03},          {0x3392, 0x1f},
    {0x3393, 0xff},          {0x3394, 0xff},
    {0x3395, 0xff},          {0x33a2, 0x04},
    {0x33ad, 0x0c},          {0x33b1, 0x20},
    {0x33b3, 0x38},          {0x33f9, 0x40},
    {0x33fb, 0x48},          {0x33fc, 0x0f},
    {0x33fd, 0x1f},          {0x349f, 0x03},
    {0x34a6, 0x03},          {0x34a7, 0x1f},
    {0x34a8, 0x38},          {0x34a9, 0x30},
    {0x34ab, 0xd0},          {0x34ad, 0xd8},
    {0x34f8, 0x1f},          {0x34f9, 0x20},
    {0x3630, 0xa0},          {0x3631, 0x92},
    {0x3632, 0x64},          {0x3633, 0x43},
    {0x3637, 0x49},          {0x363a, 0x85},
    {0x363c, 0x0f},          {0x3650, 0x31},
    {0x3670, 0x0d},          {0x3674, 0xc0},
    {0x3675, 0xa0},          {0x3676, 0xa0},
    {0x3677, 0x92},          {0x3678, 0x96},
    {0x3679, 0x9a},          {0x367c, 0x03},
    {0x367d, 0x0f},          {0x367e, 0x01},
    {0x367f, 0x0f},          {0x3698, 0x83},
    {0x3699, 0x86},          {0x369a, 0x8c},
    {0x369b, 0x94},          {0x36a2, 0x01},
    {0x36a3, 0x03},          {0x36a4, 0x07},
    {0x36ae, 0x0f},          {0x36af, 0x1f},
    {0x36bd, 0x22},          {0x36be, 0x22},
    {0x36bf, 0x22},          {0x36d0, 0x01},
    {0x370f, 0x02},          {0x3721, 0x6c},
    {0x3722, 0x8d},          {0x3725, 0xc5},
    {0x3727, 0x14},          {0x3728, 0x04},
    {0x37b7, 0x04},          {0x37b8, 0x04},
    {0x37b9, 0x06},          {0x37bd, 0x07},
    {0x37be, 0x0f},          {0x3901, 0x02},
    {0x3903, 0x40},          {0x3905, 0x8d},
    {0x3907, 0x00},          {0x3908, 0x41},
    {0x391f, 0x41},          {0x3933, 0x80},
    {0x3934, 0x02},          {0x3937, 0x6f},
    {0x393a, 0x01},          {0x393d, 0x01},
    {0x393e, 0xc0},          {0x39dd, 0x41},
    {0x3e00, 0x00},          {0x3e01, 0x4d},
    {0x3e02, 0xc0},          {0x3e09, 0x00},
    {0x4509, 0x28},          {0x450d, 0x61},
    {SC202CS_REG_END, 0x00},
};

/* ISP info for 800x600 mode - Match M5Stack Tab5 values */
static const esp_cam_sensor_isp_info_t sc202cs_800x600_isp_info = {
    .isp_v1_info = {
        .version = SENSOR_ISP_INFO_VERSION_DEFAULT,
        .pclk = 72000000,     /* Pixel clock */
        .hts = 1920,          /* Horizontal Total Size */
        .vts = 1250,          /* Vertical Total Size */
        .exp_def = 0x4dc,     /* M5Stack value (1244) - proper exposure */
        .gain_def = 0,        /* M5Stack value - no extra gain */
        .bayer_type = ESP_CAM_SENSOR_BAYER_BGGR,
    }
};

/* --------------------------------------------------------------------------
 *  Descripteur de format esp_video pour le mode 800x600
 * --------------------------------------------------------------------------*/

static const esp_cam_sensor_format_t sc202cs_custom_format_800x600 = {
    .name      = "MIPI_1lane_24Minput_RAW8_800x600_30fps",
    .format    = ESP_CAM_SENSOR_PIXFORMAT_RAW8,
    .port      = ESP_CAM_SENSOR_MIPI_CSI,
    .xclk      = 24000000,    /* 24 MHz */
    .width     = 800,
    .height    = 600,
    .regs      = init_reglist_MIPI_1lane_raw8_800x600_30fps,
    .regs_size = ARRAY_SIZE(init_reglist_MIPI_1lane_raw8_800x600_30fps),
    .fps       = 30,
    .isp_info  = &sc202cs_800x600_isp_info,
    .mipi_info = {
        .mipi_clk     = 576000000,  /* 576 Mbps, 1 lane */
        .lane_num     = 1,
        .line_sync_en = false,
    },
    .reserved  = NULL,
};

#ifdef __cplusplus
}
#endif
