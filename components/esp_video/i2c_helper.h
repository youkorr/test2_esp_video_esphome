#pragma once

#include "esphome/components/i2c/i2c.h"
#include "esphome/core/log.h"

#ifdef USE_ESP_IDF
#include "driver/i2c_master.h"

namespace esphome {
namespace esp_video {

static const char *const TAG_I2C_HELPER = "i2c_helper";

/**
 * @brief Get I2C bus handle using ESP-IDF public API
 *
 * Uses i2c_master_get_bus_handle() to retrieve the bus handle for a given port.
 * This is the official ESP-IDF API and works regardless of ESPHome's internal layout.
 */
inline i2c_master_bus_handle_t get_i2c_bus_handle_by_port(int port_num) {
  i2c_master_bus_handle_t handle = nullptr;

  esp_err_t ret = i2c_master_get_bus_handle(port_num, &handle);
  if (ret == ESP_OK && handle != nullptr) {
    ESP_LOGI(TAG_I2C_HELPER, "I2C bus handle retrieved via API for port %d: %p", port_num, handle);
    return handle;
  }

  ESP_LOGE(TAG_I2C_HELPER, "i2c_master_get_bus_handle(port=%d) failed: %s", port_num, esp_err_to_name(ret));
  return nullptr;
}

}  // namespace esp_video
}  // namespace esphome

#endif  // USE_ESP_IDF
