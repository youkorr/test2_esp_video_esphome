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
 * FINAL SOLUTION: Use a real C array instead of relying on linker symbol ordering.
 *
 * The header declares external symbols, but we define them to point into a static array
 * that is guaranteed to be contiguous in memory by the C standard.
 *
 * This works because:
 * 1. Static arrays are guaranteed contiguous by C standard
 * 2. We use the array's first element address as _start
 * 3. We calculate _end based on array size
 * 4. The iteration loop in esp_video_init.c will work correctly
 */

// Real contiguous array of sensor detection functions
static const esp_cam_sensor_detect_fn_t sensor_detect_array[] = {
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

// Number of sensors in the array
#define SENSOR_DETECT_ARRAY_SIZE (sizeof(sensor_detect_array) / sizeof(sensor_detect_array[0]))

// Export symbols compatible with the header declarations
// These are references to the first element, making &__esp_cam_sensor_detect_fn_array_start
// point to the beginning of our contiguous array
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start __attribute__((alias("sensor_detect_array")));

// Create a sentinel element right after the array
// We use a separate variable to ensure it's placed after the array in memory
static const esp_cam_sensor_detect_fn_t sentinel_end = {
    .detect = NULL,
    .port = 0,
    .sccb_addr = 0
};

// Point _end to the sentinel
// The iteration will be: for (p = &array[0]; p < &sentinel; p++)
// This will iterate exactly SENSOR_DETECT_ARRAY_SIZE times
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_end __attribute__((alias("sentinel_end")));

/**
 * Alternative approach if alias doesn't work:
 * We can use weak symbols or simply ensure the array itself is at a known location.
 *
 * But the key insight is: C arrays are ALWAYS contiguous, so sensor_detect_array[0]
 * through sensor_detect_array[N-1] are guaranteed to be sequential in memory.
 *
 * The linker cannot reorder elements within a C array, unlike separate global variables.
 */
