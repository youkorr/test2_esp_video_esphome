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
#include <stdlib.h>
#include <string.h>

/* Forward declarations of stub functions */
static esp_h264_err_t hw_stub_open(esp_h264_enc_handle_t enc);
static esp_h264_err_t hw_stub_process(esp_h264_enc_handle_t enc, esp_h264_enc_in_frame_t *in_frame, esp_h264_enc_out_frame_t *out_frame);
static esp_h264_err_t hw_stub_close(esp_h264_enc_handle_t enc);
static esp_h264_err_t hw_stub_del(esp_h264_enc_handle_t enc);

/**
 * @brief Stub open function
 */
static esp_h264_err_t hw_stub_open(esp_h264_enc_handle_t enc)
{
    (void)enc; /* Unused parameter */
    /* Hardware encoder not available - return unsupported */
    return ESP_H264_ERR_UNSUPPORTED;
}

/**
 * @brief Stub process function
 */
static esp_h264_err_t hw_stub_process(esp_h264_enc_handle_t enc, esp_h264_enc_in_frame_t *in_frame, esp_h264_enc_out_frame_t *out_frame)
{
    (void)enc;       /* Unused parameter */
    (void)in_frame;  /* Unused parameter */
    (void)out_frame; /* Unused parameter */
    /* Hardware encoder not available - return unsupported */
    return ESP_H264_ERR_UNSUPPORTED;
}

/**
 * @brief Stub close function
 */
static esp_h264_err_t hw_stub_close(esp_h264_enc_handle_t enc)
{
    (void)enc; /* Unused parameter */
    /* Hardware encoder not available - return unsupported */
    return ESP_H264_ERR_UNSUPPORTED;
}

/**
 * @brief Stub del function
 */
static esp_h264_err_t hw_stub_del(esp_h264_enc_handle_t enc)
{
    if (enc) {
        /* Free the encoder structure */
        free(enc);
    }
    return ESP_H264_ERR_OK;
}

/**
 * @brief Stub implementation of esp_h264_enc_hw_new
 *
 * This function creates a stub encoder that returns ESP_H264_ERR_UNSUPPORTED
 * for all operations, indicating that hardware H.264 encoding is not available.
 */
esp_h264_err_t esp_h264_enc_hw_new(const esp_h264_enc_cfg_hw_t *cfg, esp_h264_enc_handle_t *out_enc)
{
    /* Validate input parameters */
    if (cfg == NULL || out_enc == NULL) {
        return ESP_H264_ERR_ARG;
    }

    /* Allocate encoder structure */
    esp_h264_enc_handle_t enc = (esp_h264_enc_handle_t)malloc(sizeof(esp_h264_enc_t));
    if (enc == NULL) {
        *out_enc = NULL;
        return ESP_H264_ERR_MEM;
    }

    /* Initialize with stub functions */
    memset(enc, 0, sizeof(esp_h264_enc_t));
    enc->open = hw_stub_open;
    enc->process = hw_stub_process;
    enc->close = hw_stub_close;
    enc->del = hw_stub_del;

    *out_enc = enc;

    /* Return OK - the encoder is created but will return UNSUPPORTED on use */
    return ESP_H264_ERR_OK;
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
