/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define OV5647_REG_DELAY            0xeeee
#define OV5647_REG_END              0xffff
#define OV5647_REG_SENSOR_ID_H      0x300a
#define OV5647_REG_SENSOR_ID_L      0x300b
#define OV5647_REG_SLEEP_MODE       0x0100
#define OV5647_REG_MIPI_CTRL00      0x4800
#define OV5647_REG_FRAME_OFF_NUMBER 0x4202
#define OV5640_REG_PAD_OUT          0x300d

/* AEC/AGC controls (ported from Linux mainline drivers/media/i2c/ov5647.c). */
#define OV5647_REG_AEC_AGC          0x3503  /* bit0: AGC manual, bit1: AEC manual */
#define OV5647_REG_AEC_EXPO_H       0x3500  /* exposure 19:16 */
#define OV5647_REG_AEC_EXPO_M       0x3501  /* exposure 15:8 */
#define OV5647_REG_AEC_EXPO_L       0x3502  /* exposure 7:0 (low 4 bits are fractional) */
#define OV5647_REG_AGC_GAIN_H       0x350A  /* gain 9:8 */
#define OV5647_REG_AGC_GAIN_L       0x350B  /* gain 7:0 */

/* Bounds from the Linux mainline driver. */
#define OV5647_EXPOSURE_MIN         4
#define OV5647_EXPOSURE_STEP        1
#define OV5647_EXPOSURE_DEFAULT     1000
#define OV5647_EXPOSURE_MAX         65535
#define OV5647_GAIN_MIN             16   /* 1.0x (gain reg / 16) */
#define OV5647_GAIN_MAX             1023 /* ~64x */
#define OV5647_GAIN_STEP            1
#define OV5647_GAIN_DEFAULT         32   /* 2.0x */

#ifdef __cplusplus
}
#endif
