/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP32-P4 H.264 Profile Configuration via V4L2
 */

#pragma once

#include <stdint.h>
#include "linux/v4l2-controls.h"
#include "esp_h264_dec.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Map V4L2 H.264 profile to ESP H.264 profile
 *
 * @param v4l2_profile V4L2 profile enum value
 * @return esp_h264_profile_idc_t ESP H.264 profile value
 */
static inline esp_h264_profile_idc_t esp_video_v4l2_to_h264_profile(enum v4l2_mpeg_video_h264_profile v4l2_profile)
{
    switch (v4l2_profile) {
    case V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE:
    case V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE:
        return ESP_H264_PROFILE_BASELINE;

    case V4L2_MPEG_VIDEO_H264_PROFILE_MAIN:
        return ESP_H264_PROFILE_MAIN;

    case V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED:
        return ESP_H264_PROFILE_EXTENDED;

    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH:
    case V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH:
        return ESP_H264_PROFILE_HIGH;

    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10:
    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10_INTRA:
        return ESP_H264_PROFILE_HIGH10;

    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422:
    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422_INTRA:
        return ESP_H264_PROFILE_HIGH422;

    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_PREDICTIVE:
    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_INTRA:
    case V4L2_MPEG_VIDEO_H264_PROFILE_CAVLC_444_INTRA:
        return ESP_H264_PROFILE_HIGH444;

    default:
        return ESP_H264_PROFILE_AUTO;  // Auto-detect
    }
}

/**
 * @brief Map ESP H.264 profile to V4L2 profile
 *
 * @param esp_profile ESP H.264 profile value
 * @return enum v4l2_mpeg_video_h264_profile V4L2 profile enum
 */
static inline enum v4l2_mpeg_video_h264_profile esp_video_h264_to_v4l2_profile(esp_h264_profile_idc_t esp_profile)
{
    switch (esp_profile) {
    case ESP_H264_PROFILE_BASELINE:
        return V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;

    case ESP_H264_PROFILE_MAIN:
        return V4L2_MPEG_VIDEO_H264_PROFILE_MAIN;

    case ESP_H264_PROFILE_EXTENDED:
        return V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED;

    case ESP_H264_PROFILE_HIGH:
        return V4L2_MPEG_VIDEO_H264_PROFILE_HIGH;

    case ESP_H264_PROFILE_HIGH10:
        return V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10;

    case ESP_H264_PROFILE_HIGH422:
        return V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422;

    case ESP_H264_PROFILE_HIGH444:
        return V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_PREDICTIVE;

    case ESP_H264_PROFILE_AUTO:
    default:
        return V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;  // Safe default
    }
}

/**
 * @brief Get profile name string for logging
 *
 * @param profile ESP H.264 profile
 * @return const char* Profile name
 */
static inline const char* esp_video_h264_profile_name(esp_h264_profile_idc_t profile)
{
    switch (profile) {
    case ESP_H264_PROFILE_BASELINE:
        return "Baseline";
    case ESP_H264_PROFILE_MAIN:
        return "Main";
    case ESP_H264_PROFILE_EXTENDED:
        return "Extended";
    case ESP_H264_PROFILE_HIGH:
        return "High";
    case ESP_H264_PROFILE_HIGH10:
        return "High 10";
    case ESP_H264_PROFILE_HIGH422:
        return "High 4:2:2";
    case ESP_H264_PROFILE_HIGH444:
        return "High 4:4:4";
    case ESP_H264_PROFILE_AUTO:
        return "Auto-detect";
    default:
        return "Unknown";
    }
}

/**
 * @brief Check if profile is supported by ESP32-P4 with OpenH264
 *
 * @param profile ESP H.264 profile
 * @return true if supported, false otherwise
 */
static inline bool esp_video_h264_profile_is_supported(esp_h264_profile_idc_t profile)
{
    switch (profile) {
    case ESP_H264_PROFILE_BASELINE:
    case ESP_H264_PROFILE_MAIN:
    case ESP_H264_PROFILE_HIGH:
    case ESP_H264_PROFILE_AUTO:
        return true;  // Supported with OpenH264

    case ESP_H264_PROFILE_EXTENDED:
    case ESP_H264_PROFILE_HIGH10:
    case ESP_H264_PROFILE_HIGH422:
    case ESP_H264_PROFILE_HIGH444:
    default:
        return false;  // Not supported
    }
}

#ifdef __cplusplus
}
#endif
