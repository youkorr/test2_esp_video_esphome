#pragma once

/**
 * PPA Compatibility Header for ESP32-P4
 *
 * This header provides missing PPA color mode definitions for older ESP-IDF versions
 * Based on ESP-IDF v5.4+ PPA driver specifications
 */

#ifdef CONFIG_IDF_TARGET_ESP32P4

#include "driver/ppa.h"

// Define YUV420 color mode if not available in current ESP-IDF version
#ifndef PPA_SRM_COLOR_MODE_YUV420

/**
 * Complete PPA SRM Color Mode enum values:
 * Based on ESP-IDF latest documentation
 * Sequential enum starting from 0
 */
#define PPA_SRM_COLOR_MODE_YUV420       ((ppa_srm_color_mode_t)3)   /*!< YUV420 planar format (I420) */
#define PPA_SRM_COLOR_MODE_YUV444       ((ppa_srm_color_mode_t)4)   /*!< YUV444 format (limited range input only) */
#define PPA_SRM_COLOR_MODE_YUV422_UYVY  ((ppa_srm_color_mode_t)5)   /*!< YUV422 UYVY pack order */
#define PPA_SRM_COLOR_MODE_YUV422_VYUY  ((ppa_srm_color_mode_t)6)   /*!< YUV422 VYUY (input only) */
#define PPA_SRM_COLOR_MODE_YUV422_YUYV  ((ppa_srm_color_mode_t)7)   /*!< YUV422 YUYV (input only) */
#define PPA_SRM_COLOR_MODE_YUV422_YVYU  ((ppa_srm_color_mode_t)8)   /*!< YUV422 YVYU (input only) */
#define PPA_SRM_COLOR_MODE_GRAY8        ((ppa_srm_color_mode_t)9)   /*!< GRAY8 format */

#endif // PPA_SRM_COLOR_MODE_YUV420

#endif // CONFIG_IDF_TARGET_ESP32P4
