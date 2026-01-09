/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP32-P4 H.264 Decoder using OpenH264 library
 * This implementation supports Baseline, Main, and High H.264 profiles
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_h264_dec.h"
#include "codec_api.h"
#include "codec_app_def.h"
#include "esp_h264_check.h"
#include "esp_h264_alloc.h"
#include "esp_h264_dec_sw.h"

static const char *TAG = "H264_DEC.OPENH264";

typedef struct esp_h264_dec_openh264_handle {
    esp_h264_dec_t        base;
    esp_h264_dec_param_t  param_hd;
    uint32_t              width;
    uint32_t              height;
    ISVCDecoder          *dec_hd;      // OpenH264 decoder handle
    uint32_t              out_len;
    uint8_t              *yuv_buffer;   // Internal YUV buffer for format conversion
    uint32_t              yuv_buffer_size;
} esp_h264_dec_openh264_handle_t;

static esp_h264_err_t get_res(esp_h264_dec_param_handle_t param_hd, esp_h264_resolution_t *res)
{
    esp_h264_dec_openh264_handle_t *oh264_hd = __containerof(param_hd, esp_h264_dec_openh264_handle_t, param_hd);
    res->width = oh264_hd->width;
    res->height = oh264_hd->height;
    return ESP_H264_ERR_OK;
}

static esp_h264_err_t dec_process(esp_h264_dec_handle_t dec, esp_h264_dec_in_frame_t *in_frame, esp_h264_dec_out_frame_t *out_frame)
{
    esp_h264_dec_openh264_handle_t *oh264_hd = __containerof(dec, esp_h264_dec_openh264_handle_t, base);

    if (!oh264_hd->dec_hd || !in_frame || !out_frame) {
        ESP_H264_LOGE(TAG, "Invalid decoder handle or frame pointers");
        return ESP_H264_ERR_ARG;
    }

    SBufferInfo dst_buf_info;
    memset(&dst_buf_info, 0, sizeof(SBufferInfo));

    uint8_t *pData[3] = {NULL};

    // Decode frame using OpenH264
    DECODING_STATE ret_code = (*oh264_hd->dec_hd)->DecodeFrameNoDelay(
        oh264_hd->dec_hd,
        in_frame->raw_data.buffer,
        in_frame->raw_data.len,
        pData,
        &dst_buf_info
    );

    // Mark all input data as consumed
    in_frame->consume = in_frame->raw_data.len;
    out_frame->out_size = 0;

    // Check decoding status
    switch (ret_code) {
    case dsErrorFree:
        // Decode successful
        if (dst_buf_info.iBufferStatus == 1) {
            // Frame is ready
            oh264_hd->width = dst_buf_info.UsrData.sSystemBuffer.iWidth;
            oh264_hd->height = dst_buf_info.UsrData.sSystemBuffer.iHeight;
            oh264_hd->out_len = oh264_hd->width * oh264_hd->height + (oh264_hd->width * oh264_hd->height >> 1);

            // Check if we need to allocate/reallocate YUV buffer
            if (oh264_hd->yuv_buffer_size < oh264_hd->out_len) {
                if (oh264_hd->yuv_buffer) {
                    esp_h264_free(oh264_hd->yuv_buffer);
                }
                uint32_t actual_size;
                oh264_hd->yuv_buffer = esp_h264_calloc_prefer(1, oh264_hd->out_len, &actual_size,
                                                              ESP_H264_MEM_SPIRAM, ESP_H264_MEM_INTERNAL);
                if (!oh264_hd->yuv_buffer) {
                    ESP_H264_LOGE(TAG, "Failed to allocate YUV buffer");
                    return ESP_H264_ERR_MEM;
                }
                oh264_hd->yuv_buffer_size = actual_size;
            }

            // Copy YUV data from OpenH264 output to our buffer
            // OpenH264 outputs I420 format: Y, U, V planes
            if (pData[0] && pData[1] && pData[2]) {
                uint32_t y_size = oh264_hd->width * oh264_hd->height;
                uint32_t uv_size = y_size >> 2;

                // Copy Y plane
                memcpy(oh264_hd->yuv_buffer, pData[0], y_size);
                // Copy U plane
                memcpy(oh264_hd->yuv_buffer + y_size, pData[1], uv_size);
                // Copy V plane
                memcpy(oh264_hd->yuv_buffer + y_size + uv_size, pData[2], uv_size);

                out_frame->outbuf = oh264_hd->yuv_buffer;
                out_frame->out_size = oh264_hd->out_len;
                out_frame->pts = in_frame->pts;
                out_frame->dts = in_frame->dts;
            }
        }
        return ESP_H264_ERR_OK;

    case dsFramePending:
        // Need more data
        return ESP_H264_ERR_OK;

    case dsRefLost:
        ESP_H264_LOGE(TAG, "Reference frame lost");
        return ESP_H264_ERR_FAIL;

    case dsBitstreamError:
        ESP_H264_LOGE(TAG, "Bitstream error");
        return ESP_H264_ERR_FAIL;

    case dsDepLayerLost:
        ESP_H264_LOGE(TAG, "Dependent layer lost");
        return ESP_H264_ERR_FAIL;

    case dsNoParamSets:
        ESP_H264_LOGE(TAG, "No parameter sets (SPS/PPS missing)");
        return ESP_H264_ERR_FAIL;

    case dsDataErrorConcealed:
        ESP_H264_LOGE(TAG, "Data error concealed");
        return ESP_H264_ERR_OK;  // Continue with concealed data

    case dsInvalidArgument:
        ESP_H264_LOGE(TAG, "Invalid argument");
        return ESP_H264_ERR_ARG;

    case dsOutOfMemory:
        ESP_H264_LOGE(TAG, "Out of memory");
        return ESP_H264_ERR_MEM;

    default:
        ESP_H264_LOGE(TAG, "Unknown decoding error: %d", ret_code);
        return ESP_H264_ERR_FAIL;
    }
}

static esp_h264_err_t dec_close(esp_h264_dec_handle_t dec)
{
    return ESP_H264_ERR_OK;
}

static esp_h264_err_t dec_open(esp_h264_dec_handle_t dec)
{
    return ESP_H264_ERR_OK;
}

static esp_h264_err_t dec_del(esp_h264_dec_handle_t dec)
{
    if (dec) {
        esp_h264_dec_openh264_handle_t *oh264_hd = __containerof(dec, esp_h264_dec_openh264_handle_t, base);

        if (oh264_hd->dec_hd) {
            (*oh264_hd->dec_hd)->Uninitialize(oh264_hd->dec_hd);
            WelsDestroyDecoder(oh264_hd->dec_hd);
            oh264_hd->dec_hd = NULL;
        }

        if (oh264_hd->yuv_buffer) {
            esp_h264_free(oh264_hd->yuv_buffer);
            oh264_hd->yuv_buffer = NULL;
        }

        dec_close(dec);
        esp_h264_free(oh264_hd);
    }
    return ESP_H264_ERR_OK;
}

esp_h264_err_t esp_h264_dec_openh264_new(const esp_h264_dec_cfg_sw_t *cfg, esp_h264_dec_handle_t *out_dec)
{
    /* Parameter check */
    ESP_H264_RET_ON_FALSE(cfg && out_dec, ESP_H264_ERR_ARG, TAG, "Invalid h264 configure and handle parameter");
    ESP_H264_RET_ON_FALSE(cfg->pic_type == ESP_H264_RAW_FMT_I420, ESP_H264_ERR_ARG, TAG, "Un-supported h264 picture type parameter");

    *out_dec = NULL;

    printf("\n========================================\n");
    printf(">>> OPENH264 DECODER INITIALIZATION <<<\n");
    printf("========================================\n");

    const char *version = esp_openh264_get_version();
    printf(">>> OpenH264 library version: %s\n", version ? version : "unknown");
    ESP_H264_LOGI(TAG, "OpenH264 version: %s", version ? version : "unknown");

    /** Create decoder handle */
    uint32_t actual_size;
    esp_h264_dec_openh264_handle_t *oh264_hd = (esp_h264_dec_openh264_handle_t *)esp_h264_calloc_prefer(
        1, sizeof(esp_h264_dec_openh264_handle_t), &actual_size, ESP_H264_MEM_SPIRAM, ESP_H264_MEM_INTERNAL);
    ESP_H264_RET_ON_FALSE(oh264_hd != NULL, ESP_H264_ERR_MEM, TAG, "No memory for handle");

    /* Initialize decoder */
    esp_h264_err_t ret = ESP_H264_ERR_OK;

    // Create OpenH264 decoder
    long create_ret = WelsCreateDecoder(&oh264_hd->dec_hd);
    if (create_ret != 0 || oh264_hd->dec_hd == NULL) {
        ESP_H264_LOGE(TAG, "Failed to create OpenH264 decoder: %ld", create_ret);
        ret = ESP_H264_ERR_FAIL;
        goto __dec_exit__;
    }

    printf(">>> OpenH264 decoder created successfully\n");

    // Configure decoder parameters
    SDecodingParam dec_param;
    memset(&dec_param, 0, sizeof(SDecodingParam));
    dec_param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;  // H.264/AVC stream
    dec_param.bParseOnly = false;  // Full decoding, not parse-only
    dec_param.eEcActiveIdc = ERROR_CON_SLICE_COPY;  // Enable error concealment

    // Initialize decoder with parameters
    long init_ret = (*oh264_hd->dec_hd)->Initialize(oh264_hd->dec_hd, &dec_param);
    if (init_ret != 0) {
        ESP_H264_LOGE(TAG, "Failed to initialize OpenH264 decoder: %ld", init_ret);
        ret = ESP_H264_ERR_FAIL;
        goto __dec_exit__;
    }

    printf(">>> OpenH264 decoder initialized successfully\n");
    printf(">>> Supports H.264 Baseline, Main, and High profiles!\n");
    printf("========================================\n\n");

    ESP_H264_LOGI(TAG, "H.264 Decoder initialized with OpenH264 (supports Baseline/Main/High profiles)");

    /** Configure decoder handle */
    oh264_hd->base.open = dec_open;
    oh264_hd->base.process = dec_process;
    oh264_hd->base.close = dec_close;
    oh264_hd->base.del = dec_del;
    oh264_hd->param_hd.get_res = get_res;
    oh264_hd->yuv_buffer = NULL;
    oh264_hd->yuv_buffer_size = 0;
    *out_dec = &oh264_hd->base;
    return ret;

__dec_exit__:
    /** Delete the decoder handle */
    dec_del(&oh264_hd->base);
    return ret;
}

esp_h264_err_t esp_h264_dec_openh264_get_param_hd(esp_h264_dec_handle_t dec, esp_h264_dec_param_sw_handle_t *out_param)
{
    if (dec && out_param) {
        esp_h264_dec_openh264_handle_t *oh264_hd = __containerof(dec, esp_h264_dec_openh264_handle_t, base);
        *out_param = &oh264_hd->param_hd;
        return ESP_H264_ERR_OK;
    }
    return ESP_H264_ERR_ARG;
}
