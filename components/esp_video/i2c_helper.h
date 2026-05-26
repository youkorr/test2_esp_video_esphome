#pragma once

#include "esphome/components/i2c/i2c.h"
#include "esphome/core/log.h"

#ifdef USE_ESP_IDF
#include "driver/i2c_master.h"
#include "soc/soc_caps.h"

// Include the ESP-IDF I2C bus implementation header.
// IDFI2CBus::bus_ is protected — we access it through a derived-class
// accessor so the **compiler** computes the correct offset instead of
// hard-coding one (the old obj_ptr[10] hack that broke with ESPHome 2026.5).
#if __has_include("esphome/components/i2c/i2c_bus_esp_idf.h")
#include "esphome/components/i2c/i2c_bus_esp_idf.h"
#define ESP_VIDEO_HAS_IDFI2CBUS 1
#endif

namespace esphome {
namespace esp_video {

static const char *const TAG_I2C_HELPER = "i2c_helper";

#ifdef ESP_VIDEO_HAS_IDFI2CBUS
namespace {
// Thin derived class — adds no data members, so its layout is identical
// to IDFI2CBus.  The only purpose is to reach the *protected* bus_ field
// at the offset the compiler already knows.  This replaces the fragile
// hard-coded byte offset that broke each time ESPHome changed the class.
struct I2CBusAccessor : public i2c::IDFI2CBus {
  static i2c_master_bus_handle_t extract(i2c::I2CBus *bus) {
    auto *self = static_cast<I2CBusAccessor *>(
        static_cast<i2c::IDFI2CBus *>(bus));
    return self->bus_;
  }
};
}  // namespace
#endif  // ESP_VIDEO_HAS_IDFI2CBUS

/**
 * Retrieve the ESP-IDF i2c_master_bus_handle_t from an ESPHome I2CBus.
 *
 * Strategy (in order):
 *  1. i2c_master_get_bus_handle(preferred_port)    — if caller supplies a port
 *  2. i2c_master_get_bus_handle(get_port())        — uses IDFI2CBus::get_port()
 *  3. Compiler-computed accessor to IDFI2CBus::bus_ — always the right offset
 *  4. Scan all HP-I2C ports via i2c_master_get_bus_handle()
 *
 * Steps 2–3 require the IDFI2CBus header; if it is missing the code
 * falls back to step 4 automatically.
 */
inline i2c_master_bus_handle_t get_i2c_bus_handle(i2c::I2CBus *bus,
                                                   int8_t preferred_port = -1) {
  if (bus == nullptr) {
    ESP_LOGE(TAG_I2C_HELPER, "I2C bus pointer is nullptr");
    return nullptr;
  }

  i2c_master_bus_handle_t hdl = nullptr;

  // --- 1. User-specified port -------------------------------------------
  if (preferred_port >= 0) {
    if (i2c_master_get_bus_handle((i2c_port_num_t)preferred_port, &hdl) == ESP_OK
        && hdl != nullptr) {
      ESP_LOGI(TAG_I2C_HELPER, "I2C handle OK (user port %d): %p",
               preferred_port, hdl);
      return hdl;
    }
  }

#ifdef ESP_VIDEO_HAS_IDFI2CBUS
  auto *idf_bus = static_cast<i2c::IDFI2CBus *>(bus);

  // --- 2. IDFI2CBus::get_port() + i2c_master_get_bus_handle() -----------
  {
    i2c_port_num_t port = (i2c_port_num_t)idf_bus->get_port();
    hdl = nullptr;
    if (i2c_master_get_bus_handle(port, &hdl) == ESP_OK && hdl != nullptr) {
      ESP_LOGI(TAG_I2C_HELPER, "I2C handle OK (IDFI2CBus port %d): %p",
               (int)port, hdl);
      return hdl;
    }
  }

  // --- 3. Direct access via compiler-computed offset --------------------
  hdl = I2CBusAccessor::extract(bus);
  if (hdl != nullptr) {
    ESP_LOGI(TAG_I2C_HELPER, "I2C handle OK (direct accessor): %p", hdl);
    return hdl;
  }
#endif  // ESP_VIDEO_HAS_IDFI2CBUS

  // --- 4. Brute-force scan all HP-I2C ports -----------------------------
#ifdef SOC_HP_I2C_NUM
  constexpr int max_ports = SOC_HP_I2C_NUM;
#else
  constexpr int max_ports = 2;
#endif
  for (int port = 0; port < max_ports; port++) {
    hdl = nullptr;
    if (i2c_master_get_bus_handle((i2c_port_num_t)port, &hdl) == ESP_OK
        && hdl != nullptr) {
      ESP_LOGI(TAG_I2C_HELPER, "I2C handle OK (scan port %d): %p", port, hdl);
      return hdl;
    }
  }

  ESP_LOGE(TAG_I2C_HELPER,
           "Failed to obtain I2C bus handle (tried all methods). "
           "Try adding 'i2c_port: 0' to your esp_video config, or "
           "switch to dedicated mode (sda_pin + scl_pin).");
  return nullptr;
}

/**
 * Read a single byte from a 16-bit-addressed I2C register.
 */
inline esp_err_t i2c_read_register(i2c_master_bus_handle_t bus_handle,
                                    uint8_t device_addr,
                                    uint16_t reg_addr, uint8_t *data) {
  if (bus_handle == nullptr || data == nullptr)
    return ESP_ERR_INVALID_ARG;

  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = device_addr;
  dev_cfg.scl_speed_hz = 400000;

  i2c_master_dev_handle_t dev_handle;
  esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
  if (ret != ESP_OK)
    return ret;

  uint8_t reg_buf[2] = {(uint8_t)(reg_addr >> 8), (uint8_t)(reg_addr & 0xFF)};
  ret = i2c_master_transmit_receive(dev_handle, reg_buf, sizeof(reg_buf),
                                     data, 1, 1000);
  i2c_master_bus_rm_device(dev_handle);
  return ret;
}

}  // namespace esp_video
}  // namespace esphome

#endif  // USE_ESP_IDF
