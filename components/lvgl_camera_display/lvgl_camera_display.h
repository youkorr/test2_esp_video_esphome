#pragma once

#include "esphome/core/component.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/components/esp_cam_sensor/esp_cam_sensor_camera.h"

#ifdef CONFIG_IDF_TARGET_ESP32P4
#include "driver/ppa.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#endif

// Forward declarations
namespace esphome {
namespace face_detection {
class FaceDetectionComponent;
}
namespace yolo11_detection {
class YOLO11DetectionComponent;
}
namespace pedestrian_detection {
class PedestrianDetectionComponent;
}
}

namespace esphome {
namespace lvgl_camera_display {

class LVGLCameraDisplay : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_camera(esp_cam_sensor::MipiDSICamComponent *camera) { this->camera_ = camera; }
  void set_canvas_id(const std::string &canvas_id) { this->canvas_id_ = canvas_id; }
  void set_update_interval(uint32_t interval_ms) { this->update_interval_ = interval_ms; }
  void set_enabled(bool enabled) { this->enabled_ = enabled; }
#ifdef USE_FACE_DETECTION
  void set_face_detection(face_detection::FaceDetectionComponent *face_detect) { this->face_detection_ = face_detect; }
#endif
#ifdef USE_YOLO11_DETECTION
  void set_yolo11_detection(yolo11_detection::YOLO11DetectionComponent *yolo11_detect) { this->yolo11_detection_ = yolo11_detect; }
#endif
#ifdef USE_PEDESTRIAN_DETECTION
  void set_pedestrian_detection(pedestrian_detection::PedestrianDetectionComponent *ped_detect) { this->pedestrian_detection_ = ped_detect; }
#endif

  void configure_canvas(lv_obj_t *canvas);
  void set_stats_label(lv_obj_t *label);

  // Getters for stats (for external display)
  float get_fps() const { return this->stats_fps_; }
  float get_cpu_percent() const { return this->stats_cpu_percent_; }
  float get_frame_time() const { return this->stats_frame_time_; }

  float get_setup_priority() const override { return setup_priority::LATE; }

  // Static callback for LVGL timer
  static void lvgl_timer_callback_(lv_timer_t *timer);

 protected:
  esp_cam_sensor::MipiDSICamComponent *camera_{nullptr};
#ifdef USE_FACE_DETECTION
  face_detection::FaceDetectionComponent *face_detection_{nullptr};  // Optional
#endif
#ifdef USE_YOLO11_DETECTION
  yolo11_detection::YOLO11DetectionComponent *yolo11_detection_{nullptr};  // Optional
#endif
#ifdef USE_PEDESTRIAN_DETECTION
  pedestrian_detection::PedestrianDetectionComponent *pedestrian_detection_{nullptr};  // Optional
#endif
  lv_obj_t *canvas_obj_{nullptr};
  std::string canvas_id_{};

  uint32_t update_interval_{33};
  uint32_t last_update_{0};

  uint32_t frame_count_{0};
  bool first_update_{true};
  bool canvas_warning_shown_{false};
  bool enabled_{false};  // LVGL camera display enabled/disabled by switch

  uint32_t last_fps_time_{0};

  lv_timer_t *lvgl_timer_{nullptr};

  // Buffer pool tracking (pour release apres affichage)
  esp_cam_sensor::SimpleBufferElement *displayed_buffer_{nullptr};

  // LVGL 9.4 Zero-copy: draw buffer that points directly to camera buffer
  lv_draw_buf_t camera_draw_buf_{};
  bool draw_buf_initialized_{false};
  bool is_canvas_{false};  // true if widget is canvas (uses memcpy), false if image (uses zero-copy)

  // Async PPA pre-resize pipeline (à la Waveshare). When canvas != camera
  // size, a dedicated producer task pulls camera frames, submits a PPA SRM
  // scale in NON_BLOCKING mode, waits on a semaphore that the PPA done
  // callback gives, then pushes the resized buffer to a queue. The LVGL
  // timer callback only pops the latest ready buffer and hands it to LVGL
  // - no PPA wait happens in the LVGL task, so contention with LVGL's own
  // PPA usage (display rotation) doesn't stall the render loop.
#ifdef CONFIG_IDF_TARGET_ESP32P4
  // Minimum viable ping-pong: producer writes to one buffer while consumer
  // shows the other. Two is enough to keep both decoupled without DMA
  // contention. Going higher only buys latency, not throughput, while
  // each extra slot costs canvas_w * canvas_h * 2 bytes of SPIRAM.
  static constexpr int NUM_DISPLAY_BUFS = 2;

  ppa_client_handle_t ppa_srm_client_{nullptr};
  uint8_t *display_bufs_[NUM_DISPLAY_BUFS]{};
  uint16_t display_buf_w_{0};
  uint16_t display_buf_h_{0};
  size_t display_buf_size_{0};   // cache-line-aligned size, may be > w*h*2

  QueueHandle_t free_queue_{nullptr};    // free display buffers (producer pulls from here)
  QueueHandle_t filled_queue_{nullptr};  // ready display buffers (consumer pulls from here)
  SemaphoreHandle_t ppa_done_sem_{nullptr};
  TaskHandle_t producer_task_handle_{nullptr};
  volatile bool producer_should_run_{false};

  uint8_t *async_displayed_buf_{nullptr};  // the buffer LVGL currently shows; returned to free_queue on next swap

  bool ppa_async_enabled_{false};
  bool ppa_setup_attempted_{false};
  uint32_t ppa_error_count_{0};
#endif

#ifdef CONFIG_IDF_TARGET_ESP32P4
  bool setup_ppa_async_pipeline_(uint16_t cam_w, uint16_t cam_h,
                                  uint16_t canvas_w, uint16_t canvas_h);
  void teardown_ppa_async_pipeline_();
  bool submit_ppa_scale_(const uint8_t *src, uint16_t src_w, uint16_t src_h, uint8_t *dst);
  static bool ppa_trans_done_cb_(ppa_client_handle_t client, ppa_event_data_t *event_data, void *user_data);
  static void producer_task_entry_(void *arg);
  void producer_loop_();
#endif

  // Benchmark stats for UI display
  lv_obj_t *stats_label_{nullptr};
  float stats_fps_{0.0f};
  float stats_cpu_percent_{0.0f};
  float stats_frame_time_{0.0f};
  float stats_lvgl_overhead_{0.0f};

  void update_camera_frame_();
  void update_canvas_();
  void update_stats_label_();
};

}  // namespace lvgl_camera_display
}  // namespace esphome
