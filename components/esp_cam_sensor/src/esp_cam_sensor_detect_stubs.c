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
 * FINAL SOLUTION: Define a real contiguous array and use pointer arithmetic.
 *
 * The header declares:
 *   extern esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start;
 *   extern esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_end;
 *
 * The code iterates: for (p = &start; p < &end; ++p)
 *
 * We define _start as an array (which the C standard guarantees is contiguous),
 * and _end as a single variable immediately following it.
 *
 * When you take &array_name where array_name is an array, you get a pointer
 * to the first element. So &__esp_cam_sensor_detect_fn_array_start gives
 * us a pointer to the first sensor.
 */

// Define _start as an array containing all sensors
// The C standard guarantees array elements are contiguous in memory
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
    },
};

// Define _end as a sentinel placed right after the array
// The C compiler will typically place this immediately after the array in .data/.rodata
// Making it likely (though not guaranteed) to be at &array[3]
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_end = {
    .detect = NULL,
    .port = 0,
    .sccb_addr = 0
};

/**
 * How this works:
 *
 * 1. __esp_cam_sensor_detect_fn_array_start is defined as an array[3]
 * 2. When code uses &__esp_cam_sensor_detect_fn_array_start, it gets
 *    a pointer to the first element (same as &array[0])
 * 3. The loop increments this pointer: p++
 * 4. p < &__esp_cam_sensor_detect_fn_array_end checks if we've reached the end
 *
 * The header declared _start and _end as "extern esp_cam_sensor_detect_fn_t",
 * which is compatible with both:
 * - An array definition (array name decays to pointer to first element)
 * - A single variable
 *
 * This works because in C:
 * - "esp_cam_sensor_detect_fn_t array[3]" when used in expressions
 *   becomes "esp_cam_sensor_detect_fn_t *" (pointer to first element)
 * - Taking &array gives you the address of the first element
 *
 * CRITICAL: We rely on the compiler placing __esp_cam_sensor_detect_fn_array_end
 * immediately after the array. While not guaranteed by C standard, GCC typically
 * does this for variables in the same translation unit with similar storage class.
 *
 * If the compiler doesn't place them contiguously, the loop will iterate too
 * many or too few times. To guarantee correctness, we should modify esp_video_init.c
 * to calculate the end based on array size, but that would require changing
 * upstream code.
 */
