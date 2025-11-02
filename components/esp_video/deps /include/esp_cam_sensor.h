#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_CAM_SENSOR_MIPI_CSI = 0,
    ESP_CAM_SENSOR_DVP = 1,
} esp_cam_sensor_port_t;

typedef struct {
    void *sccb_handle;
    int reset_pin;
    int pwdn_pin;
    int xclk_pin;
    uint32_t xclk_freq_hz;
    esp_cam_sensor_port_t sensor_port;
} esp_cam_sensor_config_t;

typedef struct {
    uint16_t pid;
} esp_cam_sensor_id_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t format;
} esp_cam_sensor_format_t;

typedef struct esp_cam_sensor_device_t esp_cam_sensor_device_t;

typedef struct {
    esp_err_t (*query_para_desc)(esp_cam_sensor_device_t *dev, void *arg);
    esp_err_t (*get_para_value)(esp_cam_sensor_device_t *dev, uint32_t id, void *arg, size_t size);
    esp_err_t (*set_para_value)(esp_cam_sensor_device_t *dev, uint32_t id, const void *arg, size_t size);
    esp_err_t (*query_support_formats)(esp_cam_sensor_device_t *dev, void *arg);
    esp_err_t (*query_support_capability)(esp_cam_sensor_device_t *dev, void *arg);
    esp_err_t (*set_format)(esp_cam_sensor_device_t *dev, const void *format);
    esp_err_t (*get_format)(esp_cam_sensor_device_t *dev, void *format);
    esp_err_t (*priv_ioctl)(esp_cam_sensor_device_t *dev, uint32_t cmd, void *arg);
    esp_err_t (*del)(esp_cam_sensor_device_t *dev);
} esp_cam_sensor_ops_t;

struct esp_cam_sensor_device_t {
    char *name;
    void *sccb_handle;
    int xclk_pin;
    int reset_pin;
    int pwdn_pin;
    esp_cam_sensor_port_t sensor_port;
    esp_cam_sensor_id_t id;
    const esp_cam_sensor_ops_t *ops;
    void *priv;
    bool stream_status;
};

esp_err_t esp_cam_sensor_ioctl(esp_cam_sensor_device_t *dev, uint32_t cmd, void *arg);

#ifdef __cplusplus
}
#endif
