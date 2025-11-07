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
  // Layout typique avec héritage multiple:
  // Offset 0:  vtable (4 bytes)
  // Offset 4:  I2CBus::scan_results_ (std::vector, 12 bytes)
  // Offset 16: I2CBus::scan_ (1 byte + 3 padding)
  // Offset 20: Component::component_source_ (4 bytes)
  // Offset 24: Component::warn_if_blocking_over_ (2 bytes)
  // Offset 26: Component::component_state_ (1 byte)
  // Offset 27: Component::pending_enable_loop_ (1 byte)
  // Offset 28: IDFI2CBus::dev_ (i2c_master_dev_handle_t, 4 bytes)
  // Offset 32: IDFI2CBus::bus_ (i2c_master_bus_handle_t, 4 bytes) ← Le handle!

  void **obj_ptr = reinterpret_cast<void**>(bus);

  ESP_LOGI(TAG_I2C_HELPER, "Debug I2CBus - Analyse mémoire complète:");
  ESP_LOGI(TAG_I2C_HELPER, "  Adresse bus: %p", bus);

  // Afficher les 12 premiers pointeurs pour identifier le handle
  for (int i = 0; i < 12; i++) {
    ESP_LOGI(TAG_I2C_HELPER, "  obj_ptr[%d] (offset %d): %p", i, i*4, obj_ptr[i]);
  }

  // Selon l'analyse du code source ESPHome, bus_ devrait être à offset 32 = obj_ptr[8]
  i2c_master_bus_handle_t handle = reinterpret_cast<i2c_master_bus_handle_t>(obj_ptr[8]);

  ESP_LOGI(TAG_I2C_HELPER, "  Tentative: obj_ptr[8] (offset 32) = %p", handle);

  return handle;
}

}  // namespace esp_video
}  // namespace esphome

#endif  // USE_ESP_IDF
