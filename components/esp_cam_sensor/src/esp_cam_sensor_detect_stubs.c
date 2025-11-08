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
 * SOLUTION: Define individual structures in declaration order. GCC places symbols
 * from the same translation unit in the same section in the order they appear in
 * the source file. This is a GCC implementation detail we can rely on for this
 * specific toolchain (riscv32-esp GCC 14.2.0).
 *
 * We use numbered section names (.sensor_detect.0, .sensor_detect.1, etc.) to
 * help the linker maintain order even if it sorts sections alphabetically.
 */

// Sensor 0: OV5647 - this is __esp_cam_sensor_detect_fn_array_start
__attribute__((section(".sensor_detect.0"), used))
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start = {
    .detect = (esp_cam_sensor_device_t *(*)(void *))ov5647_detect,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .sccb_addr = OV5647_SCCB_ADDR
};

// Sensor 1: SC202CS
__attribute__((section(".sensor_detect.1"), used))
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_sc202cs = {
    .detect = (esp_cam_sensor_device_t *(*)(void *))sc202cs_detect,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .sccb_addr = SC202CS_SCCB_ADDR
};

// Sensor 2: OV02C10
__attribute__((section(".sensor_detect.2"), used))
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_ov02c10 = {
    .detect = (esp_cam_sensor_device_t *(*)(void *))ov02c10_detect,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .sccb_addr = OV02C10_SCCB_ADDR
};

// End marker - this is __esp_cam_sensor_detect_fn_array_end
__attribute__((section(".sensor_detect.3"), used))
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_end = {
    .detect = NULL,
    .port = 0,
    .sccb_addr = 0
};
