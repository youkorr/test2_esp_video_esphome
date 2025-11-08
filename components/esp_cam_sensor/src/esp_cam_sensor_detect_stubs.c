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

// Create the sensor detection array including the end marker
// We name the array __esp_cam_sensor_detect_fn_array_start to match the expected symbol
// The header declares: extern esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start;
//
// Array elements are GUARANTEED contiguous: &array[0] < &array[1] < &array[2] < &array[3]
// The loop: for (p = &array[0]; p < &array[3]; ++p) will iterate over elements 0, 1, 2
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start[] __attribute__((used)) = {
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
    // [3] - End marker (loop stops before reaching this)
    {
        .detect = NULL,
        .port = 0,
        .sccb_addr = 0
    }
};

// Define the end symbol as a reference to the 4th element of the array
// This ensures &__esp_cam_sensor_detect_fn_array_end == &__esp_cam_sensor_detect_fn_array_start[3]
// We use #define to make it an alias for the array element
#define __esp_cam_sensor_detect_fn_array_end (__esp_cam_sensor_detect_fn_array_start[3])
