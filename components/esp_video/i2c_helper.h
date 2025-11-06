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
 */
inline i2c_master_bus_handle_t get_i2c_bus_handle(i2c::I2CBus *bus) {
  if (bus == nullptr) {
    return nullptr;
  }

  // Cast vers IDFI2CBus pour accéder aux membres ESP-IDF
  auto *idf_bus = static_cast<i2c::IDFI2CBus *>(bus);
  if (idf_bus == nullptr) {
    return nullptr;
  }

  // Accès au handle via un pointeur vers le membre bus_
  // Note: Cette approche fonctionne car bus_ est le premier membre de IDFI2CBus
  // Si l'API ESPHome change, il faudra adapter cette fonction
  struct IDFI2CBusInternal {
    i2c_master_bus_handle_t bus_;
    // ... autres membres
  };

  auto *internal = reinterpret_cast<IDFI2CBusInternal *>(idf_bus);
  return internal->bus_;
}

}  // namespace esp_video
}  // namespace esphome

#endif  // USE_ESP_IDF
