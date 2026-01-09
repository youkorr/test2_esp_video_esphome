/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP32-P4 H.264 Decoder using OpenH264 library
 * This implementation supports Baseline, Main, and High H.264 profiles
 */

#pragma once

#include "esp_h264_dec.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Software decoder parameter handle (OpenH264 implementation)
 */
typedef struct esp_h264_dec_param *esp_h264_dec_param_openh264_handle_t;

/**
 * @brief Software decoder configure information (OpenH264 implementation)
 */
typedef esp_h264_dec_cfg_t esp_h264_dec_cfg_openh264_t;

/**
 * @brief  Create a new software H.264 decoder instance using OpenH264
 *
 * @param[in]   cfg      Decoder configuration
 * @param[out]  out_dec  Output decoder handle
 *
 * @return
 *       - ESP_H264_ERR_OK           Succeeded
 *       - ESP_H264_ERR_ARG          Invalid arguments
 *       - ESP_H264_ERR_MEM          Out of memory
 *       - ESP_H264_ERR_FAIL         Failed to create decoder
 *
 * @note This decoder supports H.264 Baseline, Main, and High profiles
 */
esp_h264_err_t esp_h264_dec_openh264_new(const esp_h264_dec_cfg_openh264_t *cfg, esp_h264_dec_handle_t *out_dec);

/**
 * @brief  Get parameter handle from OpenH264 decoder instance
 *
 * @param[in]   dec        Decoder handle
 * @param[out]  out_param  Output parameter handle
 *
 * @return
 *       - ESP_H264_ERR_OK   Succeeded
 *       - ESP_H264_ERR_ARG  Invalid arguments
 */
esp_h264_err_t esp_h264_dec_openh264_get_param_hd(esp_h264_dec_handle_t dec, esp_h264_dec_param_openh264_handle_t *out_param);

#ifdef __cplusplus
}
#endif
