/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include "esp_h264_dec.h"
#include "codec_api.h"          // OpenH264 API instead of h264bsd
#include "esp_h264_check.h"
#include "esp_h264_alloc.h"
#include "esp_h264_dec_sw.h"

static const char *TAG = "H264_DEC.SW";

typedef struct esp_h264_dec_sw_handle {
    esp_h264_dec_t        base;
    esp_h264_dec_param_t  param_hd;
    uint32_t              width;
    uint32_t              height;
    ISVCDecoder           *dec_hd;      // OpenH264 decoder handle (pointer to ISVCDecoder)
    uint32_t              out_len;
    unsigned char         *out_planes[3]; // Y, U, V plane pointers for OpenH264
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

    // OpenH264 buffer info structure
    SBufferInfo sDstBufInfo;
    memset(&sDstBufInfo, 0, sizeof(SBufferInfo));

    // Reset output planes
    memset(sw_hd->out_planes, 0, sizeof(sw_hd->out_planes));

    // Call OpenH264 DecodeFrameNoDelay (recommended API)
    // ISVCDecoder = const ISVCDecoderVtbl *
    // vtable->DecodeFrameNoDelay(ISVCDecoder *, buffer, len, planes, info)
    DECODING_STATE retCode = (*sw_hd->dec_hd)->DecodeFrameNoDelay(
        sw_hd->dec_hd,
        in_frame->raw_data.buffer,
        in_frame->raw_data.len,
        sw_hd->out_planes,
        &sDstBufInfo
    );

    // Assume all input data was consumed (OpenH264 doesn't report partial consumption like h264bsd)
    in_frame->consume = in_frame->raw_data.len;
    out_frame->out_size = 0;

    // Check for errors
    if (retCode != dsErrorFree) {
        if (retCode & dsBitstreamError) {
            ESP_H264_LOGE(TAG, "Bitstream error (0x%x)", retCode);
            return ESP_H264_ERR_FAIL;
        } else if (retCode & dsNoParamSets) {
            ESP_H264_LOGE(TAG, "No parameter sets (SPS/PPS missing)");
            return ESP_H264_ERR_FAIL;
        } else if (retCode & dsOutOfMemory) {
            ESP_H264_LOGE(TAG, "Out of memory");
            return ESP_H264_ERR_MEM;
        } else if (retCode & dsInvalidArgument) {
            ESP_H264_LOGE(TAG, "Invalid argument");
            return ESP_H264_ERR_ARG;
        } else if (retCode == dsFramePending) {
            // Not an error - just need more data
            return ESP_H264_ERR_OK;
        } else {
            ESP_H264_LOGE(TAG, "Decoding error (0x%x)", retCode);
            return ESP_H264_ERR_FAIL;
        }
    }

    // Check if frame is ready
    if (sDstBufInfo.iBufferStatus == 1) {
        // Frame decoded successfully
        sw_hd->width = sDstBufInfo.UsrData.sSystemBuffer.iWidth;
        sw_hd->height = sDstBufInfo.UsrData.sSystemBuffer.iHeight;

        // Calculate output size: Y + U + V planes
        // Y plane: width * height
        // U plane: (width/2) * (height/2)
        // V plane: (width/2) * (height/2)
        sw_hd->out_len = sw_hd->width * sw_hd->height + (sw_hd->width * sw_hd->height >> 1);

        // For I420 format, OpenH264 outputs pDst[0]=Y, pDst[1]=U, pDst[2]=V
        // We need to copy/reference the output data
        // Note: OpenH264 manages its own internal buffer, so we use pDst[0] directly
        out_frame->outbuf = sDstBufInfo.pDst[0];
        out_frame->out_size = sw_hd->out_len;
        out_frame->pts = in_frame->pts;
        out_frame->dts = in_frame->dts;

        ESP_H264_LOGI(TAG, "Frame decoded: %ux%u, size=%u bytes", sw_hd->width, sw_hd->height, sw_hd->out_len);
        return ESP_H264_ERR_OK;
    } else {
        // No frame output yet (e.g., SPS/PPS parsed, waiting for I-frame)
        return ESP_H264_ERR_OK;
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
        esp_h264_dec_sw_handle_t *sw_hd = __containerof(dec, esp_h264_dec_sw_handle_t, base);
        if (sw_hd->dec_hd) {
            // Uninitialize OpenH264 decoder
            // vtable->Uninitialize(ISVCDecoder *)
            (*sw_hd->dec_hd)->Uninitialize(sw_hd->dec_hd);

            // Destroy OpenH264 decoder
            // WelsDestroyDecoder(ISVCDecoder *) - takes pointer to decoder
            WelsDestroyDecoder(sw_hd->dec_hd);
            sw_hd->dec_hd = NULL;
        }
        dec_close(dec);
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
    ESP_H264_LOGI(TAG, "openh264 version: %s", esp_openh264_get_version());

    /** Create decoder handle */
    uint32_t actual_size;
    esp_h264_dec_sw_handle_t *sw_hd = (esp_h264_dec_sw_handle_t *)esp_h264_calloc_prefer(1, sizeof(esp_h264_dec_sw_handle_t), &actual_size, ESP_H264_MEM_SPIRAM, ESP_H264_MEM_INTERNAL);
    ESP_H264_RET_ON_FALSE(sw_hd != NULL, ESP_H264_ERR_MEM, TAG, "No memory for handle");

    /* Parameter initialization */
    esp_h264_err_t ret = ESP_H264_ERR_OK;

    // Create OpenH264 decoder
    // WelsCreateDecoder(ISVCDecoder **) - takes pointer to pointer
    long createRet = WelsCreateDecoder(&sw_hd->dec_hd);
    ESP_H264_GOTO_ON_FALSE(createRet == 0, ret = ESP_H264_ERR_FAIL, __dec_exit__, TAG, "Failed to create OpenH264 decoder (err=%ld)", createRet);
    ESP_H264_GOTO_ON_FALSE(sw_hd->dec_hd != NULL, ret = ESP_H264_ERR_FAIL, __dec_exit__, TAG, "OpenH264 decoder is NULL");

    // Setup OpenH264 decoding parameters
    SDecodingParam sDecParam = {0};
    sDecParam.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;  // H.264/AVC bitstream
    sDecParam.bParseOnly = false;  // Full decoding (not parse-only)
    sDecParam.eEcActiveIdc = ERROR_CON_SLICE_COPY;  // Error concealment mode

    // Initialize OpenH264 decoder
    // vtable->Initialize(ISVCDecoder *, params)
    long initRet = (*sw_hd->dec_hd)->Initialize(sw_hd->dec_hd, &sDecParam);
    ESP_H264_GOTO_ON_FALSE(initRet == 0, ret = ESP_H264_ERR_FAIL, __dec_exit__, TAG, "Failed to initialize OpenH264 decoder (err=%ld)", initRet);

    // Note: OpenH264 auto-detects H.264 profile from SPS (Baseline, Main, High, etc.)
    // The decoder outputs I420 (YUV 4:2:0 planar) format by default
    // The profile_idc parameter in cfg is now fully supported!
    ESP_H264_LOGI(TAG, "H.264 Decoder initialized (OpenH264 supports ALL profiles: Baseline/Main/High/High10/High422/High444)");

    /** Decoder handle configure */
    sw_hd->base.open = dec_open;
    sw_hd->base.process = dec_process;
    sw_hd->base.close = dec_close;
    sw_hd->base.del = dec_del;
    sw_hd->param_hd.get_res = get_res;
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
