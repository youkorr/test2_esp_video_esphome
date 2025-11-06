#pragma once

#include "esphome/components/i2c/i2c.h"

#ifdef USE_ESP_IDF
#include "driver/i2c_master.h"

namespace esphome {
namespace esp_video {

/**
 * @brief Helper pour accéder au handle I2C ESP-IDF depuis un bus ESPHome
 *
 * Cette fonction accède au membre bus_ du I2CBus ESP-IDF pour obtenir
 * le handle i2c_master_bus_handle_t nécessaire à esp_video_init().
 *
 * Note: Cette approche utilise la connaissance du layout interne de la classe
 * I2CBus d'ESPHome pour ESP-IDF. Le handle est le premier membre de la classe.
 */
inline i2c_master_bus_handle_t get_i2c_bus_handle(i2c::I2CBus *bus) {
  if (bus == nullptr) {
    return nullptr;
  }

  // Structure interne qui correspond au layout de la classe I2CBus ESP-IDF
  // Le handle i2c_master_bus_handle_t est le premier membre de la classe
  struct I2CBusInternal {
    i2c_master_bus_handle_t bus_;
    // ... autres membres que nous n'avons pas besoin d'accéder
  };

  // Accès direct au handle via reinterpret_cast
  // Cela fonctionne car bus_ est garanti être le premier membre
  auto *internal = reinterpret_cast<I2CBusInternal *>(bus);
  return internal->bus_;
}

}  // namespace esp_video
}  // namespace esphome

#endif  // USE_ESP_IDF
