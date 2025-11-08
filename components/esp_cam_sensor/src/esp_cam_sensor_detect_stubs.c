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
 * SOLUTION: Use GCC's guarantee that variables in the same section from the same
 * translation unit are placed in declaration order. All symbols go in the SAME
 * section (not numbered subsections) to ensure they're treated as a group.
 *
 * We rely on:
 * 1. All variables in the SAME .esp_cam_sensor_detect_fn section
 * 2. All from the SAME source file (this file)
 * 3. GCC places them in declaration order
 *
 * This should work even if the linker.lf fragment is not processed by PlatformIO.
 */

// Sensor 0: OV5647 - this is __esp_cam_sensor_detect_fn_array_start
__attribute__((section(".esp_cam_sensor_detect_fn"), used))
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start = {
    .detect = (esp_cam_sensor_device_t *(*)(void *))ov5647_detect,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .sccb_addr = OV5647_SCCB_ADDR
};

// Sensor 1: SC202CS
__attribute__((section(".esp_cam_sensor_detect_fn"), used))
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_sc202cs = {
    .detect = (esp_cam_sensor_device_t *(*)(void *))sc202cs_detect,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .sccb_addr = SC202CS_SCCB_ADDR
};

// Sensor 2: OV02C10
__attribute__((section(".esp_cam_sensor_detect_fn"), used))
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_ov02c10 = {
    .detect = (esp_cam_sensor_device_t *(*)(void *))ov02c10_detect,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .sccb_addr = OV02C10_SCCB_ADDR
};

// End marker - this is __esp_cam_sensor_detect_fn_array_end
// IMPORTANT: This MUST be the last variable declared in this section
__attribute__((section(".esp_cam_sensor_detect_fn"), used))
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_end = {
    .detect = NULL,
    .port = 0,
    .sccb_addr = 0
};
