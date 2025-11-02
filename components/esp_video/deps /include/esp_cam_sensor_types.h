#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Types de formats de capteur caméra
 */
typedef enum {
    ESP_CAM_SENSOR_PIXFORMAT_RAW8 = 0,
    ESP_CAM_SENSOR_PIXFORMAT_RAW10,
    ESP_CAM_SENSOR_PIXFORMAT_RAW12,
    ESP_CAM_SENSOR_PIXFORMAT_RGB565,
    ESP_CAM_SENSOR_PIXFORMAT_RGB888,
    ESP_CAM_SENSOR_PIXFORMAT_YUV422,
    ESP_CAM_SENSOR_PIXFORMAT_JPEG,
} esp_cam_sensor_format_t;

/**
 * @brief Types de capteurs
 */
typedef enum {
    ESP_CAM_SENSOR_OV2640 = 0,
    ESP_CAM_SENSOR_OV3660,
    ESP_CAM_SENSOR_OV5640,
    ESP_CAM_SENSOR_SC2336,
    ESP_CAM_SENSOR_SC202CS,
} esp_cam_sensor_model_t;

/**
 * @brief Modes de capteur
 */
typedef enum {
    ESP_CAM_SENSOR_MODE_SNAPSHOT = 0,
    ESP_CAM_SENSOR_MODE_STREAMING,
} esp_cam_sensor_mode_t;

/**
 * @brief Capacités du capteur
 */
typedef struct {
    uint16_t max_width;
    uint16_t max_height;
    uint16_t min_width;
    uint16_t min_height;
} esp_cam_sensor_capability_t;

#ifdef __cplusplus
}
#endif
