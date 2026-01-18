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
 * This function replaces the auto-detection mechanism used in ESP-IDF builds.
 * Instead of iterating over registered IPA detection functions, it directly
 * returns a pointer to the embedded JSON configuration based on sensor name.
 *
 * @param sensor_name Name of the sensor (e.g., "ov5647", "ov02c10", "sc202cs")
 * @return Pointer to IPA config, or NULL if not found
 *
 * @note For ESPHome, we use JSON-based configs embedded at compile time.
 *       The configs are stored as C strings generated from JSON files.
 */
const esp_ipa_config_t *esp_ipa_pipeline_get_config(const char *sensor_name)
{
    if (!sensor_name) {
        ESP_LOGE(TAG, "Sensor name is NULL");
        return NULL;
    }

    ESP_LOGI(TAG, "Loading IPA config for sensor: %s", sensor_name);

    // For ESPHome builds, we don't use the JSON parser yet
    // We just return NULL to disable IPA pipeline for now
    // TODO: Implement JSON parsing when needed

    // Match sensor name to embedded JSON config
    if (strcmp(sensor_name, "ov5647") == 0) {
        ESP_LOGI(TAG, "OV5647 IPA config available (JSON size: %zu bytes)", ov5647_ipa_config_json_size);
        // TODO: Parse ov5647_ipa_config_json_start and return parsed config
        // For now, return NULL to skip IPA pipeline
        ESP_LOGW(TAG, "IPA JSON parsing not implemented yet - IPA pipeline disabled");
        return NULL;
    } else if (strcmp(sensor_name, "ov02c10") == 0) {
        ESP_LOGI(TAG, "OV02C10 IPA config available (JSON size: %zu bytes)", ov02c10_ipa_config_json_size);
        // TODO: Parse ov02c10_ipa_config_json_start and return parsed config
        // For now, return NULL to skip IPA pipeline
        ESP_LOGW(TAG, "IPA JSON parsing not implemented yet - IPA pipeline disabled");
        return NULL;
    } else if (strcmp(sensor_name, "sc202cs") == 0) {
        ESP_LOGI(TAG, "SC202CS sensor detected - no IPA config available");
        // SC202CS doesn't have IPA config yet
        return NULL;
    } else {
        ESP_LOGW(TAG, "Unknown sensor: %s - no IPA config available", sensor_name);
        return NULL;
    }

    return NULL;
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
