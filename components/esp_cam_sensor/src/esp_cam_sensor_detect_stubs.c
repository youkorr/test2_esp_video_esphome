/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include "esp_cam_sensor_types.h"
#include "esp_cam_sensor_detect.h"
#include "ov5647.h"
#include "sc202cs.h"
#include "ov02c10.h"

/**
 * @brief Camera sensor detection array - ESPHome/PlatformIO implementation
 *
 * In ESP-IDF builds, the ESP_CAM_SENSOR_DETECT_FN macro creates structures with
 * section attributes that the linker script collects into a contiguous array.
 *
 * In ESPHome/PlatformIO builds, we don't have custom linker scripts, so we manually
 * create a contiguous array of detection structures here.
 *
 * esp_video_init iterates: for (p = &__esp_cam_sensor_detect_fn_array_start; p < &__esp_cam_sensor_detect_fn_array_end; ++p)
 * This expects a continuous array of esp_cam_sensor_detect_fn_t structures in memory.
 */

/**
 * The camera sensor detection array - implemented as a real contiguous array
 *
 * IMPORTANT: The code in esp_video_init.c expects this to be a contiguous array:
 *   for (p = &__esp_cam_sensor_detect_fn_array_start; p < &__esp_cam_sensor_detect_fn_array_end; ++p)
 *
 * Using separate variables with section attributes doesn't guarantee contiguity.
 * We must use an actual C array to guarantee contiguous memory layout.
 *
 * NOTE: The detect functions take esp_cam_sensor_config_t * but the union expects void *.
 * We cast them to match the expected signature.
 */

// The actual contiguous array of sensor detect structures
// The array is named to match what esp_video_init.c expects
// The loop will iterate from __esp_cam_sensor_detect_fn_array_start[0] through [2]
// and stop at __esp_cam_sensor_detect_fn_array_end (which is [3])
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start[] = {
    // Sensor 0: OV5647
    {
        .detect = (esp_cam_sensor_device_t *(*)(void *))ov5647_detect,
        .port = ESP_CAM_SENSOR_MIPI_CSI,
        .sccb_addr = OV5647_SCCB_ADDR
    },
    // Sensor 1: SC202CS
    {
        .detect = (esp_cam_sensor_device_t *(*)(void *))sc202cs_detect,
        .port = ESP_CAM_SENSOR_MIPI_CSI,
        .sccb_addr = SC202CS_SCCB_ADDR
    },
    // Sensor 2: OV02C10
    {
        .detect = (esp_cam_sensor_device_t *(*)(void *))ov02c10_detect,
        .port = ESP_CAM_SENSOR_MIPI_CSI,
        .sccb_addr = OV02C10_SCCB_ADDR
    }
};

// End marker - this is a separate structure that marks the end of the array
// The loop compares: p < &__esp_cam_sensor_detect_fn_array_end
// So this structure must be placed right after the array in memory
//
// However, C doesn't guarantee that separate global variables will be contiguous
// So we use a trick: we declare this as an extern and define it in the header
// Actually, the simplest is to just reference the array bound
#define __esp_cam_sensor_detect_fn_array_end (__esp_cam_sensor_detect_fn_array_start[3])
