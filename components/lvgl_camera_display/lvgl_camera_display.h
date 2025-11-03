#pragma once

#include "esphome/core/component.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "../mipi_dsi_cam/mipi_dsi_cam.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome {
namespace lvgl_camera_display {

class LVGLCameraDisplay : public Component {
 public:
  // ---- Méthodes ESPHome ----
  void setup() override;
  void loop() override;
  void dump_config() override;

  // ---- Liaison caméra / LVGL ----
  void set_camera(mipi_dsi_cam::MipiDsiCam *camera) { this->camera_ = camera; }
  void configure_canvas(lv_obj_t *canvas);

  // ---- Compatibilité ESPHome YAML ----
  void set_canvas_id(const std::string &canvas_id) { this->canvas_id_ = canvas_id; }
  void set_update_interval(uint32_t interval_ms) { this->update_interval_ = interval_ms; }

  float get_setup_priority() const override { return setup_priority::LATE; }

  // ---- Contrôle manuel de la tâche (optionnel) ----
  void stop_task();

 protected:
  // ---- Objets liés ----
  mipi_dsi_cam::MipiDsiCam *camera_{nullptr};
  lv_obj_t *canvas_obj_{nullptr};
  std::string canvas_id_{};

  // ---- États internes ----
  bool first_update_{true};
  bool stop_task_flag_{false};

  // ---- Paramètres et stats ----
  uint32_t update_interval_{33};  // ms, pour compatibilité YAML
  uint32_t frame_count_{0};

  // ---- FreeRTOS ----
  TaskHandle_t camera_task_handle_{nullptr};

  // ---- Méthodes internes ----
  static void camera_task_trampoline_(void *param);
  void camera_task_();
  void update_canvas_();
};

}  // namespace lvgl_camera_display
}  // namespace esphome




