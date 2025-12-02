#pragma once

#include "esphome/core/component.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/components/network_camera/network_camera.h"
#include <vector>

namespace esphome {
namespace multi_camera_display {

enum class DisplayMode {
  GRID,      // 4-camera grid view
  FULLSCREEN // Single camera fullscreen
};

class MultiCameraDisplay : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void add_camera(network_camera::NetworkCamera *camera) { this->cameras_.push_back(camera); }
  void set_canvas_id(const std::string &canvas_id) { this->canvas_id_ = canvas_id; }

  void configure_canvas(lv_obj_t *canvas);

  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  std::vector<network_camera::NetworkCamera *> cameras_;
  lv_obj_t *canvas_obj_{nullptr};
  std::string canvas_id_{};

  DisplayMode mode_{DisplayMode::GRID};
  int fullscreen_camera_index_{-1};

  // LVGL objects for grid display
  lv_obj_t *grid_container_{nullptr};
  std::vector<lv_obj_t *> thumbnail_canvases_;
  std::vector<lv_obj_t *> thumbnail_buttons_;
  std::vector<lv_color_t *> thumbnail_buffers_;  // Buffers for thumbnail canvases

  // LVGL object for fullscreen display
  lv_obj_t *fullscreen_canvas_{nullptr};
  lv_obj_t *back_button_{nullptr};
  lv_color_t *fullscreen_buffer_{nullptr};  // Buffer for fullscreen canvas

  void setup_grid_layout_();
  void setup_fullscreen_layout_();
  void show_grid_();
  void show_fullscreen_(int camera_index);

  static void thumbnail_clicked_callback_(lv_event_t *e);
  static void back_button_callback_(lv_event_t *e);
};

}  // namespace multi_camera_display
}  // namespace esphome
