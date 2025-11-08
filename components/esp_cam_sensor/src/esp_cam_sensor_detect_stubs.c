/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modified: 2025-11-08 - Force rebuild to ensure SC202CS is first in detection order
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
 * CRITICAL FIX v2: Force linker to place array and sentinel adjacently.
 *
 * The header declares:
 *   extern esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start[];
 *   extern esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_end;
 *
 * The code iterates: for (p = start; p < &end; ++p)
 *
 * PROBLEM 1 (original): Linker placed _end 15108 bytes away, causing 1259 iterations
 * ATTEMPTED FIX: Used .sensor_detect.00 and .sensor_detect.01 subsections
 * PROBLEM 2: Linker placed sections in REVERSE order (.01 before .00)!
 *   Result: _end=0x4ff40000, _start=0x4ff4000c → difference = -12 bytes
 *   Loop never executed because start > end
 *
 * SOLUTION v2: Place BOTH in the SAME section (.rodata.sensor_detect)
 * The linker will preserve declaration order within the same section.
 */

// Define _start as an array containing all sensors
// Both variables in SAME section to preserve declaration order
// ORDER MATTERS: Put most likely sensor first for faster detection
__attribute__((section(".rodata.sensor_detect"), used))
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start[] = {
    // Sensor 0: SC202CS (M5Stack Tab5 default sensor - try first!)
    {
        .detect = (esp_cam_sensor_device_t *(*)(void *))sc202cs_detect,
        .port = ESP_CAM_SENSOR_MIPI_CSI,
        .sccb_addr = SC202CS_SCCB_ADDR
    },
    // Sensor 1: OV5647
    {
        .detect = (esp_cam_sensor_device_t *(*)(void *))ov5647_detect,
        .port = ESP_CAM_SENSOR_MIPI_CSI,
        .sccb_addr = OV5647_SCCB_ADDR
    },
    // Sensor 2: OV02C10
    {
        .detect = (esp_cam_sensor_device_t *(*)(void *))ov02c10_detect,
        .port = ESP_CAM_SENSOR_MIPI_CSI,
        .sccb_addr = OV02C10_SCCB_ADDR
    },
};

// Define _end as a sentinel in the SAME section, declared immediately after array
// This should preserve declaration order: _start then _end
__attribute__((section(".rodata.sensor_detect"), used))
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_end = {
    .detect = NULL,
    .port = 0,
    .sccb_addr = 0
};

/**
 * How this works:
 *
 * 1. Both variables in section ".rodata.sensor_detect"
 * 2. Linker preserves declaration order within same section
 * 3. Array declared first, sentinel declared second
 * 4. __attribute__((used)) prevents optimization
 * 5. Loop: for (p = start; p < &end; ++p) iterates exactly 3 times
 *
 * Expected memory layout:
 *   0x4ff40xxx: __esp_cam_sensor_detect_fn_array_start[0] (SC202CS)
 *   0x4ff40xxx+12: __esp_cam_sensor_detect_fn_array_start[1] (OV5647)
 *   0x4ff40xxx+24: __esp_cam_sensor_detect_fn_array_start[2] (OV02C10)
 *   0x4ff40xxx+36: __esp_cam_sensor_detect_fn_array_end (sentinel)
 *
 * Pointer difference should be +36 bytes (3 sensors × 12 bytes)
 */
