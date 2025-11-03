#pragma once

#include "esphome/core/component.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "../mipi_dsi_cam/mipi_dsi_cam.h"

namespace esphome {
namespace lvgl_camera_display {

class LVGLCameraDisplay : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  // Liaison avec la caméra MIPI
  void set_camera(mipi_dsi_cam::MipiDsiCam *camera) { this->camera_ = camera; }

  // Configuration du canvas LVGL (à appeler depuis YAML ou code)
  void configure_canvas(lv_obj_t *canvas);

  // Paramètres optionnels
  void set_canvas_id(const std::string &canvas_id) { this->canvas_id_ = canvas_id; }
  void set_update_interval(uint32_t interval_ms) { this->update_interval_ = interval_ms; }

  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  // --- Objets liés ---
  mipi_dsi_cam::MipiDsiCam *camera_{nullptr};
  lv_obj_t *canvas_obj_{nullptr};
  std::string canvas_id_{};

  // --- Gestion du temps / FPS ---
  uint32_t update_interval_{33};  // Intervalle théorique (ms)
  uint32_t last_update_{0};
  uint32_t frame_count_{0};

  // --- États internes ---
  bool first_update_{true};
  bool canvas_warning_shown_{false};

  // --- Buffer LVGL interne ---
  uint8_t *lv_buffer_{nullptr};

  // --- Fonctions internes ---
  void update_canvas_();
};

}  // namespace lvgl_camera_display
}  // namespace esphome


