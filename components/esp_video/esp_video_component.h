#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/i2c/i2c.h"

namespace esphome {
namespace esp_video {

/**
 * @brief Composant ESPHome pour ESP-Video d'Espressif
 *
 * Ce composant initialise ESP-Video en appelant esp_video_init()
 * avec init_sccb=false pour utiliser le bus I2C d'ESPHome.
 *
 * Avantage: Pas de conflit de bus, partage propre du bus I2C avec autres composants ESPHome.
 */
class ESPVideoComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override {
    // Priorité DATA: s'exécute APRÈS I2C (BUS = 1000)
    // pour que le bus I2C ESPHome soit déjà créé
    return setup_priority::DATA;
  }

  // Setter pour le bus I2C ESPHome
  void set_i2c_bus(i2c::I2CBus *bus) { this->i2c_bus_ = bus; }

 protected:
  bool initialized_{false};
  i2c::I2CBus *i2c_bus_{nullptr};
};

}  // namespace esp_video
}  // namespace esphome
