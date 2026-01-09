/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Enable OpenH264 decoder for H.264 High Profile support
#define CONFIG_USE_OPENH264 1

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_h264_dec.h"

#ifdef CONFIG_USE_OPENH264
// Use OpenH264 for Baseline, Main, and High Profile support
#include "codec_api.h"
#include "codec_app_def.h"
#else
// Use tinyh264 for Baseline Profile only
#include "h264bsd_decoder.h"
#endif

#include "esp_h264_check.h"
#include "esp_h264_alloc.h"
#include "esp_h264_dec_sw.h"

static const char *TAG = "H264_DEC.SW";

typedef struct esp_h264_dec_sw_handle {
    esp_h264_dec_t        base;
    esp_h264_dec_param_t  param_hd;
    uint32_t              width;
    uint32_t              height;
#ifdef CONFIG_USE_OPENH264
    ISVCDecoder          *dec_hd;      // OpenH264 decoder handle
    uint8_t              *yuv_buffer;   // Internal YUV buffer for format conversion
    uint32_t              yuv_buffer_size;
#else
    h264bsd_hd_t          dec_hd;      // tinyh264 decoder handle
#endif
    uint32_t              out_len;
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

#ifdef CONFIG_USE_OPENH264
    // ============ OpenH264 Implementation (Baseline/Main/High Profile support) ============
    if (!sw_hd->dec_hd || !in_frame || !out_frame) {
        ESP_H264_LOGE(TAG, "Invalid decoder handle or frame pointers");
        return ESP_H264_ERR_ARG;
    }

    SBufferInfo dst_buf_info;
    memset(&dst_buf_info, 0, sizeof(SBufferInfo));
    uint8_t *pData[3] = {NULL};

    // Decode frame using OpenH264
    DECODING_STATE ret_code = (*sw_hd->dec_hd)->DecodeFrameNoDelay(
        sw_hd->dec_hd,
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
            sw_hd->width = dst_buf_info.UsrData.sSystemBuffer.iWidth;
            sw_hd->height = dst_buf_info.UsrData.sSystemBuffer.iHeight;
            sw_hd->out_len = sw_hd->width * sw_hd->height + (sw_hd->width * sw_hd->height >> 1);

            // Check if we need to allocate/reallocate YUV buffer
            if (sw_hd->yuv_buffer_size < sw_hd->out_len) {
                if (sw_hd->yuv_buffer) {
                    esp_h264_free(sw_hd->yuv_buffer);
                }
                uint32_t actual_size;
                sw_hd->yuv_buffer = esp_h264_calloc_prefer(1, sw_hd->out_len, &actual_size,
                                                            ESP_H264_MEM_SPIRAM, ESP_H264_MEM_INTERNAL);
                if (!sw_hd->yuv_buffer) {
                    ESP_H264_LOGE(TAG, "Failed to allocate YUV buffer");
                    return ESP_H264_ERR_MEM;
                }
                sw_hd->yuv_buffer_size = actual_size;
            }

            // Copy YUV data from OpenH264 output to our buffer
            // OpenH264 outputs I420 format: Y, U, V planes
            if (pData[0] && pData[1] && pData[2]) {
                uint32_t y_size = sw_hd->width * sw_hd->height;
                uint32_t uv_size = y_size >> 2;

                // Copy Y plane
                memcpy(sw_hd->yuv_buffer, pData[0], y_size);
                // Copy U plane
                memcpy(sw_hd->yuv_buffer + y_size, pData[1], uv_size);
                // Copy V plane
                memcpy(sw_hd->yuv_buffer + y_size + uv_size, pData[2], uv_size);

                out_frame->outbuf = sw_hd->yuv_buffer;
                out_frame->out_size = sw_hd->out_len;
                out_frame->pts = in_frame->pts;
                out_frame->dts = in_frame->dts;
            }
        }
        return ESP_H264_ERR_OK;

    case dsFramePending:
        return ESP_H264_ERR_OK;

    case dsRefLost:
        ESP_H264_LOGE(TAG, "Reference frame lost");
        return ESP_H264_ERR_FAIL;

    case dsBitstreamError:
        ESP_H264_LOGE(TAG, "Bitstream error");
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
#else
    // ============ tinyh264 Implementation (Baseline Profile only) ============
    uint32_t retCode = 3;
    uint8_t *pic = NULL;
    uint32_t length = in_frame->raw_data.len;

    retCode = h264bsdDecode(sw_hd->dec_hd, in_frame->raw_data.buffer, (u32 *)&length, &pic, (u32 *)&sw_hd->width, (u32 *)&sw_hd->height);
    in_frame->consume = in_frame->raw_data.len - length;
    out_frame->out_size = 0;
    sw_hd->out_len = sw_hd->width * sw_hd->height + (sw_hd->width * sw_hd->height >> 1);
    switch (retCode) {
    case H264BSD_ERROR:
        ESP_H264_LOGE(TAG, "Error in decoding");
        return ESP_H264_ERR_FAIL;
    case H264BSD_PARAM_SET_ERROR:
        ESP_H264_LOGE(TAG, "Serious error in decoding, failed to activate param sets");
        return ESP_H264_ERR_FAIL;
    /* The parsing of not picture data NALU is done, like SPS PPS.*/
    case H264BSD_RDY:
        return ESP_H264_ERR_OK;
    /* The parsing of picture NALU is done for, like I-frame P-frame.*/
    case H264BSD_PIC_RDY:
        out_frame->outbuf = pic;
        out_frame->out_size = sw_hd->out_len;
        out_frame->pts = in_frame->pts;
        out_frame->dts = in_frame->dts;
        return ESP_H264_ERR_OK;
    case H264BSD_MEMALLOC_ERROR:
        ESP_H264_LOGE(TAG, "Memory lack");
        return ESP_H264_ERR_MEM;
    }
    return ESP_H264_ERR_OK;
#endif
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
#ifdef CONFIG_USE_OPENH264
        if (sw_hd->dec_hd) {
            (*sw_hd->dec_hd)->Uninitialize(sw_hd->dec_hd);
            WelsDestroyDecoder(sw_hd->dec_hd);
            sw_hd->dec_hd = NULL;
        }
        if (sw_hd->yuv_buffer) {
            esp_h264_free(sw_hd->yuv_buffer);
            sw_hd->yuv_buffer = NULL;
        }
#else
        if (sw_hd->dec_hd) {
            h264bsdShutdown(sw_hd->dec_hd);
            h264bsdFree(sw_hd->dec_hd);
            sw_hd->dec_hd = NULL;
        }
#endif
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

    /** Create decoder handle */
    uint32_t actual_size;
    esp_h264_dec_sw_handle_t *sw_hd = (esp_h264_dec_sw_handle_t *)esp_h264_calloc_prefer(1, sizeof(esp_h264_dec_sw_handle_t), &actual_size, ESP_H264_MEM_SPIRAM, ESP_H264_MEM_INTERNAL);
    ESP_H264_RET_ON_FALSE(sw_hd != NULL, ESP_H264_ERR_MEM, TAG, "No memory for handle");

    /* Parameter initalization */
    esp_h264_err_t ret = ESP_H264_ERR_OK;

#ifdef CONFIG_USE_OPENH264
    // ============ OpenH264 Initialization ============
    printf("\n========================================\n");
    printf(">>> OPENH264 DECODER INITIALIZATION <<<\n");
    printf("========================================\n");

    const char *version = esp_openh264_get_version();
    printf(">>> OpenH264 library version: %s\n", version ? version : "unknown");
    ESP_H264_LOGI(TAG, "OpenH264 version: %s", version ? version : "unknown");

    // Create OpenH264 decoder
    long create_ret = WelsCreateDecoder(&sw_hd->dec_hd);
    if (create_ret != 0 || sw_hd->dec_hd == NULL) {
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
    long init_ret = (*sw_hd->dec_hd)->Initialize(sw_hd->dec_hd, &dec_param);
    if (init_ret != 0) {
        ESP_H264_LOGE(TAG, "Failed to initialize OpenH264 decoder: %ld", init_ret);
        ret = ESP_H264_ERR_FAIL;
        goto __dec_exit__;
    }

    // Initialize YUV buffer pointers
    sw_hd->yuv_buffer = NULL;
    sw_hd->yuv_buffer_size = 0;

    printf(">>> OpenH264 decoder initialized successfully\n");
    printf(">>> ✓ Supports H.264 Baseline Profile\n");
    printf(">>> ✓ Supports H.264 Main Profile\n");
    printf(">>> ✓ Supports H.264 High Profile\n");
    printf("========================================\n\n");

    ESP_H264_LOGI(TAG, "H.264 Decoder initialized with OpenH264 (supports Baseline/Main/High profiles)");

#else
    // ============ tinyh264 Initialization ============
    ESP_H264_LOGI(TAG, "tinyh264 version: %s ", esp_tinyh264_get_version());
    h264bsd_cfg_t tinyh264_cfg = H264BSD_CFG_DEFAULT();

    // CRITICAL DEBUG: Use printf() which CANNOT be filtered by logging system
    printf("\n\n");
    printf("========================================\n");
    printf(">>> CUSTOM DECODER CALLED <<<\n");
    printf(">>> esp_h264_dec_sw_new() executing from CUSTOM compiled file\n");
    printf("========================================\n");
    printf("\n");
    ESP_H264_LOGI(TAG, "esp_h264_dec_sw_new() called - checking DUAL_TASK defines...");

    // Configure dual-task decoder based on preprocessor flags
    // This enables parallel decoding on two CPU cores for better performance
#ifdef CONFIG_ESP_H264_DUAL_TASK
    printf(">>> DUAL-TASK CONFIG DEFINED <<<\n");
    ESP_H264_LOGI(TAG, "CONFIG_ESP_H264_DUAL_TASK is DEFINED!");
    tinyh264_cfg.dualTaskEnable = 1;
    #ifdef CONFIG_ESP_H264_DUAL_TASK_CORE
        tinyh264_cfg.dualTaskCore = CONFIG_ESP_H264_DUAL_TASK_CORE;
    #else
        tinyh264_cfg.dualTaskCore = 1;  // Default to core 1
    #endif
    #ifdef CONFIG_ESP_H264_DUAL_TASK_PRIORITY
        tinyh264_cfg.dualTaskPriority = CONFIG_ESP_H264_DUAL_TASK_PRIORITY;
    #else
        tinyh264_cfg.dualTaskPriority = 17;  // Library default priority
    #endif
    printf(">>> Dual-task enabled: dualTaskEnable=%u, core=%lu, priority=%lu\n",
           tinyh264_cfg.dualTaskEnable, tinyh264_cfg.dualTaskCore, tinyh264_cfg.dualTaskPriority);

    // Check TinyH264 library version
    const char *version = esp_tinyh264_get_version();
    printf(">>> TinyH264 library version: %s\n", version ? version : "unknown");

    ESP_H264_LOGI(TAG, "Dual-task H.264 decoder enabled: core=%lu, priority=%lu",
                  tinyh264_cfg.dualTaskCore, tinyh264_cfg.dualTaskPriority);
#else
    printf(">>> WARNING: DUAL-TASK NOT DEFINED - SINGLE TASK MODE <<<\n");
    ESP_H264_LOGI(TAG, "CONFIG_ESP_H264_DUAL_TASK is NOT DEFINED - using single-task!");
    ESP_H264_LOGI(TAG, "Single-task H.264 decoder (CONFIG_ESP_H264_DUAL_TASK not defined)");
#endif
    /* Note: Using tinyh264 library (h264bsd decoder) which supports H.264 Baseline profile.
     * For Main/High profile support, consider using edge264 or a full OpenH264 decoder.
     * The profile_idc parameter in cfg is kept for API compatibility. */
    ESP_H264_LOGI(TAG, "H.264 Decoder initialized (tinyh264/h264bsd supports Baseline profile)");

    printf(">>> Calling h264bsdAlloc() with config...\n");

    // Get task count BEFORE allocation
    UBaseType_t tasks_before = uxTaskGetNumberOfTasks();
    printf(">>> FreeRTOS tasks BEFORE h264bsdAlloc(): %u\n", tasks_before);

    sw_hd->dec_hd = h264bsdAlloc(&tinyh264_cfg);
    ESP_H264_GOTO_ON_FALSE(sw_hd->dec_hd != NULL, ret, __dec_exit__, TAG, "No memory for decoder handle");

    // Get task count AFTER allocation
    UBaseType_t tasks_after = uxTaskGetNumberOfTasks();
    printf(">>> FreeRTOS tasks AFTER h264bsdAlloc(): %u\n", tasks_after);

    if (tasks_after > tasks_before) {
        printf(">>> NEW TASK CREATED! Dual-task decoder is ACTIVE (%u new tasks)\n", tasks_after - tasks_before);
    } else {
        printf(">>> NO NEW TASK! Dual-task decoder NOT active (library might not support it)\n");
    }

    printf(">>> h264bsdAlloc() SUCCESS - decoder handle created\n");
    printf("========================================\n\n");
#endif // CONFIG_USE_OPENH264

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
