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

/**
 * Camera sensor detection array for ESPHome/PlatformIO
 *
 * The header declares these as single structures:
 *   extern esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start;
 *   extern esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_end;
 *
 * The code uses: for (p = &start; p < &end; ++p)
 *
 * CRITICAL ISSUE with section attributes:
 * The linker can place items in a section in ANY ORDER. If the linker places
 * __esp_cam_sensor_detect_fn_array_end BEFORE __esp_cam_sensor_detect_fn_array_start
 * in memory, the loop condition (p < &end) will be FALSE from the start and
 * the loop will never execute!
 *
 * SOLUTION: Use a guaranteed contiguous array in a structure, then alias the
 * individual elements to match the expected symbol names.
 */

// Create a structure containing the sensor array to ensure contiguity and order
// We declare this structure with a unique name to avoid conflicts
static struct {
    esp_cam_sensor_detect_fn_t sensors[4];  // 3 sensors + 1 end marker
} sensor_array_container __attribute__((used)) = {
    .sensors = {
        // [0] - OV5647
        {
            .detect = (esp_cam_sensor_device_t *(*)(void *))ov5647_detect,
            .port = ESP_CAM_SENSOR_MIPI_CSI,
            .sccb_addr = OV5647_SCCB_ADDR
        },
        // [1] - SC202CS
        {
            .detect = (esp_cam_sensor_device_t *(*)(void *))sc202cs_detect,
            .port = ESP_CAM_SENSOR_MIPI_CSI,
            .sccb_addr = SC202CS_SCCB_ADDR
        },
        // [2] - OV02C10
        {
            .detect = (esp_cam_sensor_device_t *(*)(void *))ov02c10_detect,
            .port = ESP_CAM_SENSOR_MIPI_CSI,
            .sccb_addr = OV02C10_SCCB_ADDR
        },
        // [3] - End marker
        {
            .detect = NULL,
            .port = 0,
            .sccb_addr = 0
        }
    }
};

// Map the expected symbol names to array elements using macros
// This is the simplest approach that guarantees correct ordering
#define __esp_cam_sensor_detect_fn_array_start (sensor_array_container.sensors[0])
#define __esp_cam_sensor_detect_fn_array_end (sensor_array_container.sensors[3])
