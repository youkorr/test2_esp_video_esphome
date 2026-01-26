#include "lvgl_camera_display.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <cstring>
// Conditionally include detection components only if they exist
#ifdef USE_FACE_DETECTION
#include "esphome/components/face_detection/face_detection.h"
#endif
#ifdef USE_YOLO11_DETECTION
#include "esphome/components/yolo11_detection/yolo11_detection.h"
#endif
#ifdef USE_PEDESTRIAN_DETECTION
#include "esphome/components/pedestrian_detection/pedestrian_detection.h"
#endif

namespace esphome {
namespace lvgl_camera_display {

static const char *const TAG = "lvgl_camera_display";

void LVGLCameraDisplay::setup() {
  ESP_LOGCONFIG(TAG, "Configuration LVGL Camera Display...");
  ESP_LOGI(TAG, "Display is DISABLED by default - enable via switch in Home Assistant");

  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "Camera non configuree");
    this->mark_failed();
    return;
  }

  // Verifier que la camera est operationnelle
  if (!this->camera_->is_pipeline_ready()) {
    ESP_LOGE(TAG, "Camera non operationnelle - pipeline non demarre");
    ESP_LOGE(TAG, "   Le composant mipi_dsi_cam a echoue a s'initialiser");
    ESP_LOGE(TAG, "   Verifiez les logs de mipi_dsi_cam pour plus de details");
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "LVGL Camera Display initialise (not started yet)");
  ESP_LOGI(TAG, "   Camera: Operationnelle");
  ESP_LOGI(TAG, "   Update interval: %u ms (~%d FPS) via LVGL timer",
           this->update_interval_, 1000 / this->update_interval_);
  ESP_LOGI(TAG, "Turn on the 'LVGL Camera Display' switch to start");
}

void LVGLCameraDisplay::loop() {
  // Start timer when enabled
  if (this->enabled_ && this->lvgl_timer_ == nullptr) {
    ESP_LOGI(TAG, "Starting LVGL Camera Display...");
    this->lvgl_timer_ = lv_timer_create(lvgl_timer_callback_, this->update_interval_, this);
    if (this->lvgl_timer_ == nullptr) {
      ESP_LOGE(TAG, "Failed to create LVGL timer");
    } else {
      ESP_LOGI(TAG, "LVGL Camera Display started");
    }
  }

  // Stop timer when disabled
  if (!this->enabled_ && this->lvgl_timer_ != nullptr) {
    ESP_LOGI(TAG, "Stopping LVGL Camera Display...");
    lv_timer_del(this->lvgl_timer_);
    this->lvgl_timer_ = nullptr;
    ESP_LOGI(TAG, "LVGL Camera Display stopped");
  }
}

// Callback du timer LVGL (appele periodiquement par LVGL)
void LVGLCameraDisplay::lvgl_timer_callback_(lv_timer_t *timer) {
  LVGLCameraDisplay *display = static_cast<LVGLCameraDisplay *>(lv_timer_get_user_data(timer));
  if (display != nullptr) {
    display->update_camera_frame_();
  }
}

// Mise a jour de la frame camera (appelee par le timer LVGL)
void LVGLCameraDisplay::update_camera_frame_() {
  // Si la camera est en streaming, capturer ET mettre a jour le canvas
  if (!this->camera_->is_streaming()) {
    return;
  }

  // Statistiques de frames manquees
  static uint32_t attempts = 0;
  static uint32_t skipped = 0;

  uint32_t t1 = millis();
  bool frame_captured = this->camera_->capture_frame();
  uint32_t t2 = millis();

  attempts++;
  if (!frame_captured) {
    skipped++;
    return;
  }

  this->update_canvas_();
  uint32_t t3 = millis();
  this->frame_count_++;

  // Accumuler les temps pour statistiques
  static uint32_t last_time = 0;
  static uint32_t total_capture_ms = 0;
  static uint32_t total_canvas_ms = 0;

  total_capture_ms += (t2 - t1);
  total_canvas_ms += (t3 - t2);

  // Logger performance toutes les 100 frames
  if (this->frame_count_ % 100 == 0) {
    uint32_t now_time = millis();

    if (last_time > 0) {
      float elapsed = (now_time - last_time) / 1000.0f;  // secondes
      float fps = 100.0f / elapsed;
      float avg_capture = total_capture_ms / 100.0f;
      float avg_canvas = total_canvas_ms / 100.0f;
      float skip_rate = (skipped * 100.0f) / attempts;
      ESP_LOGI(TAG, "%u frames - FPS: %.2f | capture: %.1fms | canvas: %.1fms | skip: %.1f%%",
               this->frame_count_, fps, avg_capture, avg_canvas, skip_rate);
    }
    last_time = now_time;
    total_capture_ms = 0;
    total_canvas_ms = 0;
    attempts = 0;
    skipped = 0;
  }
}

void LVGLCameraDisplay::dump_config() {
  ESP_LOGCONFIG(TAG, "LVGL Camera Display:");
  ESP_LOGCONFIG(TAG, "  Update interval: %u ms", this->update_interval_);
  ESP_LOGCONFIG(TAG, "  FPS cible: ~%d", 1000 / this->update_interval_);
  ESP_LOGCONFIG(TAG, "  Canvas configure: %s", this->canvas_obj_ ? "OUI" : "NON");
}

void LVGLCameraDisplay::update_canvas_() {
  if (this->camera_ == nullptr) {
    return;
  }

  if (this->canvas_obj_ == nullptr) {
    if (!this->canvas_warning_shown_) {
      ESP_LOGW(TAG, "Canvas null - pas encore configure?");
      this->canvas_warning_shown_ = true;
    }
    return;
  }

  // Liberer l'ancien buffer affiche (si present)
  if (this->displayed_buffer_ != nullptr) {
    this->camera_->release_buffer(this->displayed_buffer_);
    this->displayed_buffer_ = nullptr;
  }

  // Acquerir le nouveau buffer depuis le pool
  esp_cam_sensor::SimpleBufferElement *buffer = this->camera_->acquire_buffer();
  if (buffer == nullptr) {
    // Pas de buffer disponible - garder l'affichage precedent
    return;
  }

  uint8_t* img_data = this->camera_->get_buffer_data(buffer);
  uint16_t width = this->camera_->get_image_width();
  uint16_t height = this->camera_->get_image_height();

  if (img_data == nullptr) {
    return;
  }

  // Optional: draw detection results if configured
#ifdef USE_FACE_DETECTION
  if (this->face_detection_ != nullptr) {
    this->face_detection_->draw_on_frame(img_data, width, height);
  }
#endif
#ifdef USE_YOLO11_DETECTION
  if (this->yolo11_detection_ != nullptr) {
    this->yolo11_detection_->draw_on_frame(img_data, width, height);
  }
#endif
#ifdef USE_PEDESTRIAN_DETECTION
  if (this->pedestrian_detection_ != nullptr) {
    this->pedestrian_detection_->draw_on_frame(img_data, width, height);
  }
#endif

  if (this->first_update_) {
    ESP_LOGI(TAG, "Premier update canvas (buffer pool):");
    ESP_LOGI(TAG, "   Dimensions: %ux%u", width, height);
    ESP_LOGI(TAG, "   Buffer: %p (index=%u)", img_data, this->camera_->get_buffer_index(buffer));
    ESP_LOGI(TAG, "   Premiers pixels (RGB565): %02X%02X %02X%02X %02X%02X",
             img_data[0], img_data[1], img_data[2], img_data[3], img_data[4], img_data[5]);
    this->first_update_ = false;
  }

  // LVGL 9.4 FIX: Instead of replacing the canvas buffer (which causes crashes),
  // COPY the camera data into the canvas's existing buffer.
  // The canvas has its own buffer allocated during creation.

  // Get the canvas's draw buffer
  lv_draw_buf_t *canvas_buf = lv_canvas_get_draw_buf(this->canvas_obj_);
  if (canvas_buf == nullptr) {
    ESP_LOGE(TAG, "Canvas draw_buf is null!");
    return;
  }
  if (canvas_buf->data == nullptr) {
    ESP_LOGE(TAG, "Canvas buffer data is null!");
    ESP_LOGE(TAG, "   Canvas dimensions: %u x %u", canvas_buf->header.w, canvas_buf->header.h);
    ESP_LOGE(TAG, "   Canvas format: %d, stride: %u", canvas_buf->header.cf, canvas_buf->header.stride);
    return;
  }

  // Log canvas buffer info on first update
  if (this->first_update_) {
    ESP_LOGI(TAG, "Canvas buffer info:");
    ESP_LOGI(TAG, "   Canvas dimensions: %u x %u", canvas_buf->header.w, canvas_buf->header.h);
    ESP_LOGI(TAG, "   Canvas format: %d, stride: %u", canvas_buf->header.cf, canvas_buf->header.stride);
    ESP_LOGI(TAG, "   Canvas data ptr: %p", canvas_buf->data);
    ESP_LOGI(TAG, "   Camera dimensions: %u x %u", width, height);
  }

  // Verify dimensions match
  if (canvas_buf->header.w != width || canvas_buf->header.h != height) {
    ESP_LOGW(TAG, "Canvas size (%ux%u) != camera size (%ux%u)!",
             canvas_buf->header.w, canvas_buf->header.h, width, height);
  }

  // Calculate buffer size
  uint32_t buf_size = width * height * 2;  // RGB565 = 2 bytes per pixel

  // PERFORMANCE NOTE: memcpy with aligned buffers
  // Now that both buffers are 64-byte aligned (canvas via lv_malloc_core,
  // camera via heap_caps_aligned_alloc), memcpy should use optimized
  // cache-aligned transfers which are much faster than unaligned copies.
  //
  // ESP32-P4 memcpy performance with 64-byte aligned buffers:
  // - Unaligned: ~30 MB/s (19ms for 600KB)
  // - Aligned: ~100-150 MB/s (4-6ms for 600KB)
  //
  // For even better performance (200+ MB/s), we could use esp_async_memcpy
  // but it requires setup/initialization. The aligned memcpy is good enough
  // to reach 25-30 FPS target.
  memcpy(canvas_buf->data, img_data, buf_size);

  // Invalidate to trigger redraw
  lv_obj_invalidate(this->canvas_obj_);

  // Tracker ce buffer pour le liberer au prochain update
  this->displayed_buffer_ = buffer;
}

void LVGLCameraDisplay::configure_canvas(lv_obj_t *canvas) {
  this->canvas_obj_ = canvas;
  ESP_LOGI(TAG, "Canvas configure: %p", canvas);

  if (canvas != nullptr) {
    lv_coord_t w = lv_obj_get_width(canvas);
    lv_coord_t h = lv_obj_get_height(canvas);
    ESP_LOGI(TAG, "   Taille canvas: %dx%d", w, h);
  }
}

}  // namespace lvgl_camera_display
}  // namespace esphome
