#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"

#ifdef USE_ESP_IDF

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

extern "C" {
#include "esp_lcd_panel_ops.h"
#include "driver/mipi_dsi.h"
#include <fcntl.h>
#include <unistd.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
}

namespace esphome {
namespace dma_camera_display {

/**
 * DMA Camera Display - Zero LVGL Redraw
 *
 * Architecture esp_brookesia:
 * - Camera DMA → Buffer A (PSRAM aligned)
 * - Swap atomique → Buffer B
 * - LCD DMA ← Buffer B (esp_lcd_panel_draw_bitmap)
 * - Hardware VSync sync
 *
 * ZERO LVGL interaction:
 * - No lv_canvas
 * - No lv_obj_invalidate()
 * - No lv_refr_now()
 * - Direct DMA only
 */
class DmaCameraDisplay : public Component {
 public:
  void set_width(int w) { width_ = w; }
  void set_height(int h) { height_ = h; }
  void set_x_offset(int x) { x_offset_ = x; }
  void set_y_offset(int y) { y_offset_ = y; }
  void set_enable_vsync(bool enable) { enable_vsync_ = enable; }
  void set_fps_target(float fps) { fps_target_ = fps; }

  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override { return setup_priority::LATE; }

  // Control methods
  void start_display();
  void stop_display();
  bool is_displaying() const { return displaying_; }

 protected:
  // LCD Panel access
  bool init_lcd_panel_();
  esp_lcd_panel_handle_t get_lcd_panel_from_lvgl_();

  // Camera V4L2
  bool init_camera_v4l2_();
  void cleanup_camera_();

  // Triple buffering
  bool alloc_triple_buffers_();
  void free_triple_buffers_();

  // DMA tasks
  static void camera_task_(void *arg);
  static void display_task_(void *arg);

  // Hardware sync
  void wait_vsync_();

  // Configuration
  int width_{800};
  int height_{600};
  int x_offset_{0};
  int y_offset_{0};
  bool enable_vsync_{true};
  float fps_target_{30.0f};

  // LCD Panel (retrieved from LVGL or created)
  esp_lcd_panel_handle_t lcd_panel_{nullptr};
  bool lcd_panel_owned_{false};  // true if we created it, false if from LVGL

  // Camera V4L2
  int video_fd_{-1};
  struct {
    void *start;
    size_t length;
  } v4l2_buffers_[4];
  unsigned int v4l2_buffer_count_{0};

  // Triple buffering (PSRAM aligned 64 bytes)
  uint8_t *camera_buffer_{nullptr};    // Buffer currently being written by camera
  uint8_t *display_buffer_{nullptr};   // Buffer currently being sent to LCD
  uint8_t *swap_buffer_{nullptr};      // Buffer for atomic swap
  size_t buffer_size_{0};

  // Synchronization
  SemaphoreHandle_t frame_ready_sem_{nullptr};  // Camera frame ready
  SemaphoreHandle_t swap_mutex_{nullptr};       // Protect buffer swap
  TaskHandle_t camera_task_handle_{nullptr};
  TaskHandle_t display_task_handle_{nullptr};

  volatile bool displaying_{false};
  volatile bool tasks_running_{false};

  // FPS tracking
  uint32_t frame_count_{0};
  uint32_t last_fps_time_{0};
  float current_fps_{0.0f};
};

}  // namespace dma_camera_display
}  // namespace esphome

#endif  // USE_ESP_IDF
