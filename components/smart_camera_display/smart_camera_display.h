#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/automation.h"

#ifdef USE_ESP_IDF

#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

extern "C" {
#include <fcntl.h>
#include <unistd.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
}

namespace esphome {
namespace smart_camera_display {

/**
 * Smart Camera Display - LVGL + DMA Hybride
 *
 * Combine:
 * - Intégration LVGL (parent_id, canvas, pages)
 * - Performance DMA (triple buffering interne)
 * - API simple (comme simple_video_player)
 *
 * Usage:
 *   smart_camera_display:
 *     id: my_camera
 *     camera_model: sc202cs
 *     width: 800
 *     height: 600
 *     parent_id: camera_page
 *     display_canvas: camera_canvas
 *     show_controls: true
 */
class SmartCameraDisplay : public Component {
 public:
  // Configuration setters
  void set_camera_model(const std::string &model) { camera_model_ = model; }
  void set_width(int w) { width_ = w; }
  void set_height(int h) { height_ = h; }
  void set_x_offset(int x) { x_offset_ = x; }
  void set_y_offset(int y) { y_offset_ = y; }
  void set_parent(lv_obj_t *parent) { parent_ = parent; }
  void set_canvas(lv_obj_t *canvas) { canvas_ = canvas; }
  void set_show_controls(bool show) { show_controls_ = show; }
  void set_auto_start(bool auto_start) { auto_start_ = auto_start; }
  void set_enable_dma_optimization(bool enable) { enable_dma_opt_ = enable; }
  void set_target_fps(float fps) { target_fps_ = fps; }

  // Component lifecycle
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // Control methods (appelables depuis lambda YAML)
  void start_streaming();
  void stop_streaming();
  bool is_streaming() const { return streaming_; }

  // Getters pour lambda YAML
  int get_width() const { return width_; }
  int get_height() const { return height_; }
  float get_current_fps() const { return current_fps_; }
  std::string get_camera_model() const { return camera_model_; }

 protected:
  // Camera V4L2
  bool init_camera_();
  void cleanup_camera_();

  // Triple buffering
  bool alloc_buffers_();
  void free_buffers_();

  // Capture task
  static void capture_task_(void *arg);

  // LVGL UI
  void create_controls_();
  void update_canvas_();

  // Callbacks boutons (si show_controls: true)
  static void play_btn_cb_(lv_event_t *e);
  static void pause_btn_cb_(lv_event_t *e);
  static void stop_btn_cb_(lv_event_t *e);
  static void info_btn_cb_(lv_event_t *e);

  // Configuration
  std::string camera_model_{"sc202cs"};
  int width_{800};
  int height_{600};
  int x_offset_{0};
  int y_offset_{0};
  bool show_controls_{false};
  bool auto_start_{false};
  bool enable_dma_opt_{true};
  float target_fps_{30.0f};

  // LVGL objects
  lv_obj_t *parent_{nullptr};
  lv_obj_t *canvas_{nullptr};
  lv_obj_t *play_btn_{nullptr};
  lv_obj_t *pause_btn_{nullptr};
  lv_obj_t *stop_btn_{nullptr};
  lv_obj_t *info_btn_{nullptr};
  lv_obj_t *status_label_{nullptr};
  lv_obj_t *fps_label_{nullptr};

  // Camera V4L2
  int video_fd_{-1};
  struct {
    void *start;
    size_t length;
  } v4l2_buffers_[4];
  unsigned int v4l2_buffer_count_{0};

  // Triple buffering (si enable_dma_opt: true)
  uint8_t *camera_buffer_{nullptr};    // Buffer caméra (DMA write)
  uint8_t *display_buffer_{nullptr};   // Buffer canvas (LVGL read)
  uint8_t *swap_buffer_{nullptr};      // Buffer swap
  size_t buffer_size_{0};

  // Synchronization
  SemaphoreHandle_t frame_ready_sem_{nullptr};
  SemaphoreHandle_t swap_mutex_{nullptr};
  TaskHandle_t capture_task_handle_{nullptr};

  volatile bool streaming_{false};
  volatile bool task_running_{false};

  // FPS tracking
  uint32_t frame_count_{0};
  uint32_t last_fps_time_{0};
  float current_fps_{0.0f};

  // lv_img_dsc pour canvas
  lv_img_dsc_t img_dsc_{};
};

// Actions ESPHome
template<typename... Ts> class StartStreamingAction : public Action<Ts...>, public Parented<SmartCameraDisplay> {
 public:
  void play(Ts... x) override { this->parent_->start_streaming(); }
};

template<typename... Ts> class StopStreamingAction : public Action<Ts...>, public Parented<SmartCameraDisplay> {
 public:
  void play(Ts... x) override { this->parent_->stop_streaming(); }
};

}  // namespace smart_camera_display
}  // namespace esphome

#endif  // USE_ESP_IDF
