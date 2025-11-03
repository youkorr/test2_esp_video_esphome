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
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_camera(mipi_dsi_cam::MipiDsiCam *camera) { this->camera_ = camera; }
  void configure_canvas(lv_obj_t *canvas);

  float get_setup_priority() const override { return setup_priority::LATE; }

  void stop_task();  // pour arrêter proprement la tâche (optionnel)

 protected:
  // Objets liés
  mipi_dsi_cam::MipiDsiCam *camera_{nullptr};
  lv_obj_t *canvas_obj_{nullptr};

  // État
  bool first_update_{true};
  bool stop_task_flag_{false};

  // Stats
  uint32_t update_interval_{33};
  uint32_t frame_count_{0};

  // FreeRTOS
  TaskHandle_t camera_task_handle_{nullptr};

  // Méthodes internes
  static void camera_task_trampoline_(void *param);
  void camera_task_();
  void update_canvas_();
};

}  // namespace lvgl_camera_display
}  // namespace esphome



