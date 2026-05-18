/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_cam_sensor_types.h"

#define OV5647_SCCB_ADDR   0x36

/**
 * @brief Power on camera sensor device and detect the device connected to the designated sccb bus.
 *
 * @param[in] config Configuration related to device power-on and detection.
 * @return
 *      - Camera device handle on success, otherwise, failed.
 */
esp_cam_sensor_device_t *ov5647_detect(esp_cam_sensor_config_t *config);

/**
 * @brief Get the array of native sensor formats supported by the OV5647 driver.
 *
 * The returned pointer is valid for the lifetime of the program (the array is
 * static const). Use one of the entries with VIDIOC_S_SENSOR_FMT to switch the
 * sensor to a different native resolution/format.
 *
 * @param[out] count Number of entries in the returned array (may be NULL).
 * @return Pointer to the first element of the native format array.
 */
const esp_cam_sensor_format_t *ov5647_get_format_info(size_t *count);

#ifdef __cplusplus
}
#endif
