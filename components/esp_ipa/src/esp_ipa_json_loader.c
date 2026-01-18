/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-FileCopyrightText: ESPHome JSON IPA Configuration Loader
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

/**
 * @file esp_ipa_json_loader.c
 * @brief IPA configuration loader from embedded JSON files for ESPHome
 *
 * This file provides esp_ipa_pipeline_get_config() implementation that loads
 * IPA configurations from embedded JSON files instead of using auto-detection.
 *
 * The JSON files are embedded at compile time by esp_video_build.py:
 * - ov5647_default.json -> ov5647_ipa_config_json_start
 * - ov02c10_default.json -> ov02c10_ipa_config_json_start
 */

#include <string.h>
#include "esp_ipa.h"
#include "esp_log.h"

static const char *TAG = "ipa_json";

// External symbols for embedded JSON configs (created by esp_video_build.py)
extern const char ov5647_ipa_config_json_start[];
extern const char *ov5647_ipa_config_json_end;
extern const size_t ov5647_ipa_config_json_size;

extern const char ov02c10_ipa_config_json_start[];
extern const char *ov02c10_ipa_config_json_end;
extern const size_t ov02c10_ipa_config_json_size;

/**
 * @brief Get IPA configuration for a specific sensor
 *
 * This function provides ISP/IPA configurations for camera sensors.
 *
 * In ESP-IDF builds:
 * - IPAs are auto-detected via ESP_IPA_DETECT_FN() macro
 * - Linker creates detection array automatically
 * - This function iterates array to find matching sensor
 *
 * In ESPHome/PlatformIO builds:
 * - Linker doesn't support custom sections → no auto-detection
 * - JSON configs are embedded but not parsed (would need cJSON + 2000 lines mapping)
 * - Returning NULL = ISP/IPA pipeline disabled
 *
 * **IMPACT OF RETURNING NULL:**
 * - ✅ Camera works normally (capture, H.264 encoding, streaming)
 * - ❌ No automatic white balance (colors may look off)
 * - ❌ No automatic exposure (may be too bright/dark)
 * - ❌ No noise reduction (grainy in low light)
 * - ❌ No auto sharpness/gamma/contrast
 *
 * Camera produces **functional but unoptimized** images.
 *
 * @param sensor_name Name of the sensor (e.g., "ov5647", "ov02c10", "sc202cs")
 * @return Pointer to IPA config (esp_ipa_config_t*), or NULL to disable ISP/IPA
 *
 * @note To enable ISP/IPA, we would need to:
 *       1. Parse embedded JSON (ov5647_ipa_config_json_start, etc.)
 *       2. Convert JSON to 50+ nested C structures
 *       3. Allocate and populate esp_ipa_config_t with all parameters
 *       This is complex but doable if image quality becomes critical.
 */
const esp_ipa_config_t *esp_ipa_pipeline_get_config(const char *sensor_name)
{
    if (!sensor_name) {
        ESP_LOGE(TAG, "Sensor name is NULL");
        return NULL;
    }

    // Log once per sensor type to avoid spam
    static bool logged_ov5647 = false;
    static bool logged_ov02c10 = false;
    static bool logged_sc202cs = false;

    // Match sensor name to embedded JSON config
    if (strcmp(sensor_name, "ov5647") == 0) {
        if (!logged_ov5647) {
            ESP_LOGW(TAG, "OV5647: ISP/IPA pipeline DISABLED (JSON config available but not parsed)");
            ESP_LOGW(TAG, "       Camera will work with RAW image quality (no AWB/AGC/AEN)");
            ESP_LOGW(TAG, "       JSON size: %zu bytes - see ISP_IPA_PIPELINE_ANALYSIS.md", ov5647_ipa_config_json_size);
            logged_ov5647 = true;
        }
        return NULL;  // Disable ISP/IPA for now
    } else if (strcmp(sensor_name, "ov02c10") == 0) {
        if (!logged_ov02c10) {
            ESP_LOGW(TAG, "OV02C10: ISP/IPA pipeline DISABLED (JSON config available but not parsed)");
            ESP_LOGW(TAG, "        Camera will work with RAW image quality (no AWB/AGC/AEN)");
            ESP_LOGW(TAG, "        JSON size: %zu bytes - see ISP_IPA_PIPELINE_ANALYSIS.md", ov02c10_ipa_config_json_size);
            logged_ov02c10 = true;
        }
        return NULL;  // Disable ISP/IPA for now
    } else if (strcmp(sensor_name, "sc202cs") == 0) {
        if (!logged_sc202cs) {
            ESP_LOGW(TAG, "SC202CS: ISP/IPA pipeline DISABLED (no config available)");
            ESP_LOGW(TAG, "        Camera will work with RAW image quality");
            logged_sc202cs = true;
        }
        return NULL;  // No config for SC202CS
    } else {
        ESP_LOGW(TAG, "Unknown sensor '%s': ISP/IPA pipeline disabled", sensor_name);
        return NULL;
    }
}

/**
 * DESIGN NOTES:
 *
 * 1. JSON Configs:
 *    - Embedded at compile time by esp_video_build.py
 *    - Stored as C string literals in libesp_video_full.a
 *    - Format: const char sensor_ipa_config_json_start[] = "{ json content }"
 *
 * 2. Future Enhancement:
 *    - Add cJSON or similar parser to convert JSON to esp_ipa_config_t struct
 *    - Cache parsed configs to avoid re-parsing
 *    - Support runtime config updates via YAML
 *
 * 3. Why return NULL for now:
 *    - IPA pipeline is optional for basic camera operation
 *    - Camera will work without IPA (just no auto white balance, etc.)
 *    - Allows compilation to succeed while we develop JSON parser
 *    - User can still get images, just without ISP enhancements
 *
 * 4. When to implement JSON parsing:
 *    - If user complains about image quality (white balance, exposure, etc.)
 *    - If user specifically requests ISP pipeline features
 *    - For now, basic camera operation is sufficient
 */
