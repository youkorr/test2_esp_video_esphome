/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#include <stddef.h>
#include "esp_ipa.h"

/**
 * @brief Get IPA pipeline configuration for a sensor
 *
 * NOTE: This is a stub implementation for ESPHome/PlatformIO builds.
 * The full pipeline-based IPA configuration requires complex setup that's
 * handled differently in ESP-IDF vs ESPHome.
 *
 * Instead, use the JSON-based IPA loader:
 *   - esp_ipa_load_json_config() to load calibrated parameters from JSON
 *   - esp_ipa_apply_json_to_isp() to apply them via V4L2 ioctl
 *
 * This provides the same functionality (CCM, AWB, Sharpen, Gamma, Contrast)
 * without the complexity of the full pipeline system.
 *
 * @param sensor_name Sensor name (e.g., "OV5647", "OV02C10")
 * @return NULL (pipeline not used in ESPHome builds)
 */
const esp_ipa_config_t *esp_ipa_pipeline_get_config(const char *sensor_name)
{
    (void)sensor_name;  // Unused in stub

    // Return NULL to indicate pipeline is not available
    // Users should use esp_ipa_json_loader.h functions instead:
    //
    //   esp_ipa_json_config_t ipa_config;
    //   esp_ipa_load_json_config("OV5647", &ipa_config);
    //
    //   int isp_fd = open("/dev/video0", O_RDWR);
    //   esp_ipa_apply_json_to_isp(isp_fd, &ipa_config);
    //   close(isp_fd);
    //
    return NULL;
}
