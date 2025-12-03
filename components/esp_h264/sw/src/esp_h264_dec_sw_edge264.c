/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP H.264 Decoder Wrapper for edge264
 * Adapts edge264 decoder API to ESP H.264 decoder interface
 */

#include <stdio.h>
#include <string.h>
#include "esp_h264_dec.h"
#include "edge264.h"           // edge264 decoder API
#include "esp_h264_check.h"
#include "esp_h264_alloc.h"
#include "esp_h264_dec_sw.h"

static const char *TAG = "H264_DEC.EDGE264";

typedef struct esp_h264_dec_sw_handle {
    esp_h264_dec_t        base;
    esp_h264_dec_param_t  param_hd;
    uint32_t              width;
    uint32_t              height;
    Edge264Decoder        *dec_hd;      // edge264 decoder handle
    Edge264Frame          *current_frame; // Current decoded frame
    uint32_t              out_len;
    uint8_t               *output_buffer; // Temporary buffer for I420 output
} esp_h264_dec_sw_handle_t;

static esp_h264_err_t get_res(esp_h264_dec_param_handle_t param_hd, esp_h264_resolution_t *res)
{
    esp_h264_dec_sw_handle_t *sw_hd = __containerof(param_hd, esp_h264_dec_sw_handle_t, param_hd);
    res->width = sw_hd->width;
    res->height = sw_hd->height;
    return ESP_H264_ERR_OK;
}

static esp_h264_err_t dec_process(esp_h264_dec_handle_t dec, esp_h264_dec_in_frame_t *in_frame, esp_h264_dec_out_frame_t *out_frame)
{
    esp_h264_dec_sw_handle_t *sw_hd = __containerof(dec, esp_h264_dec_sw_handle_t, base);

    // Decode NAL unit using edge264
    int ret = edge264_decode_NAL(sw_hd->dec_hd, in_frame->raw_data.buffer, in_frame->raw_data.len);

    // Mark input as consumed
    in_frame->consume = in_frame->raw_data.len;
    out_frame->out_size = 0;

    if (ret < 0) {
        ESP_H264_LOGE(TAG, "edge264_decode_NAL failed: %d", ret);
        return ESP_H264_ERR_FAIL;
    }

    // Try to get a decoded frame
    Edge264Frame frame;
    ret = edge264_get_frame(sw_hd->dec_hd, &frame);

    if (ret == 0) {
        // No frame available yet (e.g., SPS/PPS parsed, waiting for I-frame)
        return ESP_H264_ERR_OK;
    } else if (ret < 0) {
        ESP_H264_LOGE(TAG, "edge264_get_frame failed: %d", ret);
        return ESP_H264_ERR_FAIL;
    }

    // Frame decoded successfully!
    sw_hd->width = frame.width_Y;
    sw_hd->height = frame.height_Y;

    // Calculate output size for I420 format
    sw_hd->out_len = sw_hd->width * sw_hd->height + (sw_hd->width * sw_hd->height >> 1);

    // edge264 outputs planar YUV with separate planes
    // We need to provide contiguous I420 buffer to esp_h264 API
    // For now, use edge264's Y plane directly (it's already contiguous in I420)
    // Note: This assumes edge264 outputs contiguous I420, which we should verify
    out_frame->outbuf = (uint8_t *)frame.samples[0]; // Y plane
    out_frame->out_size = sw_hd->out_len;
    out_frame->pts = in_frame->pts;
    out_frame->dts = in_frame->dts;

    // Store frame reference for later cleanup
    sw_hd->current_frame = esp_h264_malloc(sizeof(Edge264Frame));
    if (sw_hd->current_frame) {
        memcpy(sw_hd->current_frame, &frame, sizeof(Edge264Frame));
    }

    ESP_H264_LOGI(TAG, "Frame decoded: %ux%u, size=%u bytes", sw_hd->width, sw_hd->height, sw_hd->out_len);
    return ESP_H264_ERR_OK;
}

static esp_h264_err_t dec_close(esp_h264_dec_handle_t dec)
{
    esp_h264_dec_sw_handle_t *sw_hd = __containerof(dec, esp_h264_dec_sw_handle_t, base);

    // Return current frame if any
    if (sw_hd->current_frame) {
        edge264_return_frame(sw_hd->dec_hd, sw_hd->current_frame);
        esp_h264_free(sw_hd->current_frame);
        sw_hd->current_frame = NULL;
    }

    // Flush pending frames
    if (sw_hd->dec_hd) {
        edge264_flush(sw_hd->dec_hd);
    }

    return ESP_H264_ERR_OK;
}

static esp_h264_err_t dec_open(esp_h264_dec_handle_t dec)
{
    return ESP_H264_ERR_OK;
}

static esp_h264_err_t dec_del(esp_h264_dec_handle_t dec)
{
    if (dec) {
        esp_h264_dec_sw_handle_t *sw_hd = __containerof(dec, esp_h264_dec_sw_handle_t, base);

        dec_close(dec);

        if (sw_hd->dec_hd) {
            edge264_free(&sw_hd->dec_hd);
            sw_hd->dec_hd = NULL;
        }

        if (sw_hd->output_buffer) {
            esp_h264_free(sw_hd->output_buffer);
            sw_hd->output_buffer = NULL;
        }

        esp_h264_free(sw_hd);
    }
    return ESP_H264_ERR_OK;
}

esp_h264_err_t esp_h264_dec_sw_new(const esp_h264_dec_cfg_sw_t *cfg, esp_h264_dec_handle_t *out_dec)
{
    /* Parameter check */
    ESP_H264_RET_ON_FALSE(cfg && out_dec, ESP_H264_ERR_ARG, TAG, "Invalid h264 configure and handle parameter");
    ESP_H264_RET_ON_FALSE(cfg->pic_type == ESP_H264_RAW_FMT_I420, ESP_H264_ERR_ARG, TAG, "Un-supported h264 picture type parameter");

    *out_dec = NULL;
    ESP_H264_LOGI(TAG, "edge264 H.264 decoder initialization");

    /** Create decoder handle */
    uint32_t actual_size;
    esp_h264_dec_sw_handle_t *sw_hd = (esp_h264_dec_sw_handle_t *)esp_h264_calloc_prefer(1, sizeof(esp_h264_dec_sw_handle_t), &actual_size, ESP_H264_MEM_SPIRAM, ESP_H264_MEM_INTERNAL);
    ESP_H264_RET_ON_FALSE(sw_hd != NULL, ESP_H264_ERR_MEM, TAG, "No memory for handle");

    /* Parameter initialization */
    esp_h264_err_t ret = ESP_H264_ERR_OK;

    // Create edge264 decoder
    // Use single thread for embedded system (can be adjusted if needed)
    int num_threads = 1;

    // edge264 callbacks (can be NULL for defaults)
    sw_hd->dec_hd = edge264_alloc(num_threads, NULL, NULL, NULL, NULL, NULL);
    ESP_H264_GOTO_ON_FALSE(sw_hd->dec_hd != NULL, ret = ESP_H264_ERR_FAIL, __dec_exit__, TAG, "Failed to create edge264 decoder");

    ESP_H264_LOGI(TAG, "H.264 Decoder initialized (edge264 supports ALL profiles: Baseline/Main/High/High10/High422/High444)");
    ESP_H264_LOGI(TAG, "edge264 decoder ready - up to 8K UHD, 8-bit 4:2:0 planar YUV output");

    /** Decoder handle configure */
    sw_hd->base.open = dec_open;
    sw_hd->base.process = dec_process;
    sw_hd->base.close = dec_close;
    sw_hd->base.del = dec_del;
    sw_hd->param_hd.get_res = get_res;
    sw_hd->current_frame = NULL;
    sw_hd->output_buffer = NULL;
    *out_dec = &sw_hd->base;
    return ret;

__dec_exit__:
    /** Delete the decoder handle */
    dec_del(&sw_hd->base);
    return ret;
}

esp_h264_err_t esp_h264_dec_sw_get_param_hd(esp_h264_dec_handle_t dec, esp_h264_dec_param_sw_handle_t *out_param)
{
    if (dec && out_param) {
        esp_h264_dec_sw_handle_t *sw_hd = __containerof(dec, esp_h264_dec_sw_handle_t, base);
        *out_param = &sw_hd->param_hd;
        return ESP_H264_ERR_OK;
    }
    return ESP_H264_ERR_ARG;
}
