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
 * CRITICAL FIX: Force linker to place array and sentinel adjacently using section attributes.
 *
 * The header declares:
 *   extern esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start[];
 *   extern esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_end;
 *
 * The code iterates: for (p = start; p < &end; ++p)
 *
 * PROBLEM: The linker was placing _end 15108 bytes away from _start, causing
 * the loop to iterate through 1259 garbage entries instead of 3 valid sensors.
 *
 * SOLUTION: Use explicit section attributes with ordering (.sensor_detect.00, .sensor_detect.01)
 * to force the linker to place them adjacently. The linker sorts sections alphabetically,
 * so .00 comes before .01, guaranteeing correct placement.
 */

// Define _start as an array containing all sensors
// Use section .sensor_detect.00 to ensure it comes first
// ORDER MATTERS: Put most likely sensor first for faster detection
__attribute__((section(".sensor_detect.00"), used))
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

// Define _end as a sentinel placed right after the array
// Use section .sensor_detect.01 to ensure it comes immediately after .00
// The linker will place sections in alphabetical order: .00 then .01
__attribute__((section(".sensor_detect.01"), used))
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_end = {
    .detect = NULL,
    .port = 0,
    .sccb_addr = 0
};

/**
 * How this works:
 *
 * 1. __esp_cam_sensor_detect_fn_array_start is in section .sensor_detect.00
 * 2. __esp_cam_sensor_detect_fn_array_end is in section .sensor_detect.01
 * 3. The linker places sections in alphabetical order, so .00 comes before .01
 * 4. With used attribute, the linker won't optimize these away
 * 5. This guarantees _end is placed immediately after _start in memory
 * 6. The loop for (p = start; p < &end; ++p) will iterate exactly 3 times
 *
 * Expected memory layout:
 *   Address 0x4ff1390c: __esp_cam_sensor_detect_fn_array_start[0] (SC202CS)
 *   Address 0x4ff13918: __esp_cam_sensor_detect_fn_array_start[1] (OV5647)
 *   Address 0x4ff13924: __esp_cam_sensor_detect_fn_array_start[2] (OV02C10)
 *   Address 0x4ff13930: __esp_cam_sensor_detect_fn_array_end (sentinel)
 *
 * Pointer difference should be 36 bytes (3 sensors × 12 bytes), not 15108 bytes!
 */
