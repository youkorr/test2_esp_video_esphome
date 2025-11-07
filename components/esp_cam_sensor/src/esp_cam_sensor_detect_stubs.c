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
 * The camera sensor detection array - contiguous structures
 *
 * These must be defined as individual structures (not an array) to match the extern declarations.
 * By defining them sequentially in the same file, the compiler typically places them contiguously
 * in memory, allowing iteration from &start to &end.
 */
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start = {
    .detect = ov5647_detect,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .sccb_addr = OV5647_SCCB_ADDR
};

esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_sensor_sc202cs = {
    .detect = sc202cs_detect,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .sccb_addr = SC202CS_SCCB_ADDR
};

esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_sensor_ov02c10 = {
    .detect = ov02c10_detect,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .sccb_addr = OV02C10_SCCB_ADDR
};

/**
 * End marker - dummy structure placed after the last sensor entry.
 * The code iterates: for (p = &start; p < &end; ++p)
 */
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_end = {
    .detect = NULL,
    .port = 0,
    .sccb_addr = 0
};
