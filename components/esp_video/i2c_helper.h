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

  // Essayons de lire directement la mémoire après le vtable et les membres de Component
  // Selon le code ESPHome, le handle devrait être quelque part dans l'objet

  // Pour le moment, essayons un offset de 8 bytes (après vtable)
  void **obj_ptr = reinterpret_cast<void**>(bus);

  ESP_LOGI(TAG_I2C_HELPER, "Debug I2CBus:");
  ESP_LOGI(TAG_I2C_HELPER, "  Adresse bus: %p", bus);
  ESP_LOGI(TAG_I2C_HELPER, "  obj_ptr[0] (vtable): %p", obj_ptr[0]);
  ESP_LOGI(TAG_I2C_HELPER, "  obj_ptr[1]: %p", obj_ptr[1]);
  ESP_LOGI(TAG_I2C_HELPER, "  obj_ptr[2]: %p", obj_ptr[2]);
  ESP_LOGI(TAG_I2C_HELPER, "  obj_ptr[3]: %p", obj_ptr[3]);
  ESP_LOGI(TAG_I2C_HELPER, "  obj_ptr[4]: %p", obj_ptr[4]);
  ESP_LOGI(TAG_I2C_HELPER, "  obj_ptr[5]: %p", obj_ptr[5]);
  ESP_LOGI(TAG_I2C_HELPER, "  obj_ptr[6]: %p", obj_ptr[6]);

  // TENTATIVE 2: obj_ptr[2] au lieu de obj_ptr[1]
  // obj_ptr[1] a causé un crash, essayons obj_ptr[2]
  i2c_master_bus_handle_t handle = reinterpret_cast<i2c_master_bus_handle_t>(obj_ptr[2]);

  ESP_LOGI(TAG_I2C_HELPER, "  Handle retourné (obj_ptr[2]): %p", handle);

  return handle;
}

}  // namespace esp_video
}  // namespace esphome

#endif  // USE_ESP_IDF
