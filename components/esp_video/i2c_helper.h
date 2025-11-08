#pragma once

#include "esphome/components/i2c/i2c.h"
#include "esphome/core/log.h"

#ifdef USE_ESP_IDF
#include "driver/i2c_master.h"

namespace esphome {
namespace esp_video {

static const char *const TAG_I2C_HELPER = "i2c_helper";

/**
 * @brief Helper pour accéder au handle I2C ESP-IDF depuis un bus ESPHome
 *
 * Cette fonction accède au membre bus_ du I2CBus ESP-IDF pour obtenir
 * le handle i2c_master_bus_handle_t nécessaire à esp_video_init().
 *
 * Note: Cette approche utilise la connaissance du layout interne de la classe
 * I2CBus d'ESPHome pour ESP-IDF.
 */
inline i2c_master_bus_handle_t get_i2c_bus_handle(i2c::I2CBus *bus) {
  if (bus == nullptr) {
    ESP_LOGE(TAG_I2C_HELPER, "Bus I2C est nullptr");
    return nullptr;
  }

  // IMPORTANT: Accès aux membres de I2CBus via mémoire
  // Pour ESP-IDF, I2CBus dérive de Component donc il y a des données avant bus_

  // Tentative 1: Vérifier plusieurs offsets possibles
  // Component a typiquement un vtable (8 bytes) et quelques membres

  // Essayons d'accéder au handle qui pourrait être à différents offsets
  struct I2CBusLayout {
    void *vtable;  // 8 bytes sur ESP32
    // D'autres membres de Component...
    // Puis finalement:
    // i2c_master_bus_handle_t bus_;
  };

  // Structure mémoire de IDFI2CBus (ESP32 32-bit):
  // class IDFI2CBus : public InternalI2CBus, public Component
  //
  // Avec héritage multiple, il y a 2 vtables!
  // Offset 0:  vtable #1 pour InternalI2CBus/I2CBus (4 bytes)
  // Offset 4:  I2CBus::scan_results_ (std::vector, 12 bytes)
  // Offset 16: I2CBus::scan_ (1 byte + 3 padding)
  // Offset 20: vtable #2 pour Component (4 bytes)
  // Offset 24: Component::component_source_ (4 bytes)
  // Offset 28: Component::warn_if_blocking_over_ + state + pending (4 bytes)
  // Offset 32: padding/alignment (4 bytes)
  // Offset 36: IDFI2CBus::dev_ (i2c_master_dev_handle_t, 4 bytes)
  // Offset 40: IDFI2CBus::bus_ (i2c_master_bus_handle_t, 4 bytes) ← Le handle!

  void **obj_ptr = reinterpret_cast<void**>(bus);

  // Le membre bus_ est à offset 40 = obj_ptr[10]
  i2c_master_bus_handle_t handle = reinterpret_cast<i2c_master_bus_handle_t>(obj_ptr[10]);

  ESP_LOGI(TAG_I2C_HELPER, "Handle I2C extrait (obj_ptr[10], offset 40): %p", handle);

  return handle;
}

/**
 * @brief Read a register from an I2C device
 *
 * @param bus_handle I2C bus handle
 * @param device_addr 7-bit I2C device address
 * @param reg_addr 16-bit register address
 * @param data Pointer to store read data
 * @return ESP_OK on success, error code otherwise
 */
inline esp_err_t i2c_read_register(i2c_master_bus_handle_t bus_handle, uint8_t device_addr,
                                    uint16_t reg_addr, uint8_t *data) {
  if (bus_handle == nullptr || data == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  // Create device handle for this transaction
  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = device_addr;
  dev_cfg.scl_speed_hz = 400000;

  i2c_master_dev_handle_t dev_handle;
  esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
  if (ret != ESP_OK) {
    return ret;
  }

  // Write register address (2 bytes, big endian)
  uint8_t reg_buf[2] = {(uint8_t)(reg_addr >> 8), (uint8_t)(reg_addr & 0xFF)};

  // Perform write-read transaction
  ret = i2c_master_transmit_receive(dev_handle, reg_buf, sizeof(reg_buf), data, 1, 1000);

  // Remove device handle
  i2c_master_bus_rm_device(dev_handle);

  return ret;
}

}  // namespace esp_video
}  // namespace esphome

#endif  // USE_ESP_IDF
