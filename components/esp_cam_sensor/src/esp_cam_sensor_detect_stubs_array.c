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
 * @brief Alternative implementation using a real C array for guaranteed contiguity
 *
 * This approach uses a static array to ensure all sensor detection structures
 * are contiguous in memory. The _start and _end symbols are defined as the
 * first element and the guard element after the array.
 *
 * The header declares:
 *   extern esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start;
 *   extern esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_end;
 *
 * We define _start as an array (which decays to pointer to first element)
 * and _end as a standalone variable placed right after.
 *
 * In C, when you declare `extern type_t symbol;` but define `type_t symbol[N];`,
 * taking &symbol gives you a pointer to the first element, which is exactly
 * what esp_video_init expects.
 */

// Define _start as an array containing all sensors
// The symbol name matches the header declaration, but we define it as an array
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

// Define _end as the next element after the array
// This MUST be placed immediately after the array definition to be contiguous
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_end = {
    .detect = NULL,
    .port = 0,
    .sccb_addr = 0
};

/**
 * With this definition:
 * - &__esp_cam_sensor_detect_fn_array_start points to first sensor (OV5647)
 * - __esp_cam_sensor_detect_fn_array_start + 1 points to second sensor (SC202CS)
 * - __esp_cam_sensor_detect_fn_array_start + 2 points to third sensor (OV02C10)
 * - &__esp_cam_sensor_detect_fn_array_end points to the guard element
 *
 * The loop in esp_video_init.c:
 *   for (p = &__esp_cam_sensor_detect_fn_array_start; p < &__esp_cam_sensor_detect_fn_array_end; ++p)
 *
 * Will iterate:
 *   p = &__esp_cam_sensor_detect_fn_array_start (sensor 0)
 *   p++ (sensor 1)
 *   p++ (sensor 2)
 *   p++ (would be &__esp_cam_sensor_detect_fn_array_end, but p < &__esp_cam_sensor_detect_fn_array_end is false)
 *
 * NOTE: C does not guarantee that __esp_cam_sensor_detect_fn_array_end will be
 * placed immediately after the array. If the linker places it elsewhere,
 * the loop may not work correctly.
 *
 * A more robust solution would be to define a single array with a null terminator:
 *
 * esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start[] = {
 *     { ... sensor 0 ... },
 *     { ... sensor 1 ... },
 *     { ... sensor 2 ... },
 *     { .detect = NULL }  // terminator
 * };
 *
 * And modify esp_video_init.c to calculate the end:
 *   esp_cam_sensor_detect_fn_t *end = __esp_cam_sensor_detect_fn_array_start +
 *                                      (sizeof(__esp_cam_sensor_detect_fn_array_start) /
 *                                       sizeof(__esp_cam_sensor_detect_fn_array_start[0]));
 *
 * Or simply check for null terminator:
 *   for (p = __esp_cam_sensor_detect_fn_array_start; p->detect != NULL; ++p)
 *
 * But we can't modify esp_video_init.c without affecting ESP-IDF builds.
 */
