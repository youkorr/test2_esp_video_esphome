#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_cam_sensor_xclk_config_t {
    gpio_num_t xclk_pin;
    uint32_t xclk_freq_hz;
} esp_cam_sensor_xclk_config_t;

esp_err_t esp_cam_sensor_xclk_init(esp_cam_sensor_xclk_config_t *config);
esp_err_t esp_cam_sensor_xclk_deinit(void);

#ifdef __cplusplus
}
#endif
