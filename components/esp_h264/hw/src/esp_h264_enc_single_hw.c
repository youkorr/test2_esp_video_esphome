/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_h264_enc_single_hw.c
 * @brief Stub implementation for ESP32 hardware H.264 encoder
 *
 * This file provides stub implementations for hardware H.264 encoder functions
 * to allow compilation when the actual hardware encoder implementation is not available.
 */

#include "esp_h264_enc_single_hw.h"
#include <stddef.h>

/**
 * @brief Stub implementation of esp_h264_enc_hw_new
 *
 * This function is a stub that returns ESP_H264_ERR_UNSUPPORTED to indicate
 * that hardware H.264 encoding is not available in this build.
 */
esp_h264_err_t esp_h264_enc_hw_new(const esp_h264_enc_cfg_hw_t *cfg, esp_h264_enc_handle_t *out_enc)
{
    /* Validate input parameters */
    if (cfg == NULL || out_enc == NULL) {
        return ESP_H264_ERR_ARG;
    }

    /* Set output to NULL to indicate no encoder was created */
    *out_enc = NULL;

    /* Return unsupported error - hardware encoding not available */
    return ESP_H264_ERR_UNSUPPORTED;
}

/**
 * @brief Stub implementation of esp_h264_enc_hw_get_param_hd
 *
 * This function is a stub that returns ESP_H264_ERR_UNSUPPORTED to indicate
 * that hardware H.264 encoding is not available in this build.
 */
esp_h264_err_t esp_h264_enc_hw_get_param_hd(esp_h264_enc_handle_t enc, esp_h264_enc_param_hw_handle_t *out_param)
{
    /* Validate input parameters */
    if (enc == NULL || out_param == NULL) {
        return ESP_H264_ERR_ARG;
    }

    /* Set output to NULL to indicate no parameter handle available */
    *out_param = NULL;

    /* Return unsupported error - hardware encoding not available */
    return ESP_H264_ERR_UNSUPPORTED;
}
