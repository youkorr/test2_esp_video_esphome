#pragma once

#include "esphome/components/i2c/i2c.h"
#include "esphome/core/log.h"

#ifdef USE_ESP_IDF
#include "driver/i2c_master.h"
#include "soc/soc_caps.h"

namespace esphome {
namespace esp_video {

static const char *const TAG_I2C_HELPER = "i2c_helper";

/**
 * @brief Retrieve the ESP-IDF I2C master bus handle via the official API.
 *
 * Uses i2c_master_get_bus_handle() — a stable ESP-IDF function that looks
 * up a previously-created master bus by port number.  The caller can supply
 * a preferred port (from the YAML config); if that fails, every HP-I2C
 * port on the SoC is tried.
 *
 * The old approach of reading the handle at a hard-coded memory offset
 * inside ESPHome's IDFI2CBus object is gone — it broke whenever ESPHome
 * changed the class layout (e.g. 2026.5.0 / ESP-IDF 6.0).
 */
inline i2c_master_bus_handle_t get_i2c_bus_handle(i2c::I2CBus *bus,
                                                   int8_t preferred_port = -1) {
  if (bus == nullptr) {
    ESP_LOGE(TAG_I2C_HELPER, "I2C bus pointer is nullptr");
    return nullptr;
  }

  i2c_master_bus_handle_t hdl = nullptr;

  // 1. Try the user-specified port first.
  if (preferred_port >= 0) {
    if (i2c_master_get_bus_handle((i2c_port_num_t)preferred_port, &hdl) == ESP_OK
        && hdl != nullptr) {
      ESP_LOGI(TAG_I2C_HELPER,
               "I2C handle via i2c_master_get_bus_handle(port=%d): %p",
               preferred_port, hdl);
      return hdl;
    }
  }

  // 2. Scan every HP-I2C port the SoC exposes.
#ifdef SOC_HP_I2C_NUM
  constexpr int max_ports = SOC_HP_I2C_NUM;
#else
  constexpr int max_ports = 2;
#endif
  for (int port = 0; port < max_ports; port++) {
    hdl = nullptr;
    if (i2c_master_get_bus_handle((i2c_port_num_t)port, &hdl) == ESP_OK
        && hdl != nullptr) {
      ESP_LOGI(TAG_I2C_HELPER,
               "I2C handle via i2c_master_get_bus_handle(port=%d): %p",
               port, hdl);
      return hdl;
    }
  }

  ESP_LOGE(TAG_I2C_HELPER,
           "Could not obtain I2C bus handle from any port (tried %d ports). "
           "Consider using dedicated mode (sda_pin / scl_pin) instead of i2c_id.",
           max_ports);
  return nullptr;
}

/**
 * @brief Read a single byte from a 16-bit-addressed I2C register.
 */
inline esp_err_t i2c_read_register(i2c_master_bus_handle_t bus_handle,
                                    uint8_t device_addr,
                                    uint16_t reg_addr, uint8_t *data) {
  if (bus_handle == nullptr || data == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = device_addr;
  dev_cfg.scl_speed_hz = 400000;

  i2c_master_dev_handle_t dev_handle;
  esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
  if (ret != ESP_OK) {
    return ret;
  }

  uint8_t reg_buf[2] = {(uint8_t)(reg_addr >> 8), (uint8_t)(reg_addr & 0xFF)};
  ret = i2c_master_transmit_receive(dev_handle, reg_buf, sizeof(reg_buf),
                                     data, 1, 1000);

  i2c_master_bus_rm_device(dev_handle);
  return ret;
}

}  // namespace esp_video
}  // namespace esphome

#endif  // USE_ESP_IDF
