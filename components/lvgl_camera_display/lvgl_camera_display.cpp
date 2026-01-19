#include "lvgl_camera_display.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
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

  // Register frame callback (V4L2 Snippet #3 - automatic streaming via FreeRTOS task)
  this->camera_->set_frame_callback([this](uint8_t* buffer, uint32_t size, uint32_t index) {
    this->on_frame_callback_(buffer, size, index);
  });

  ESP_LOGI(TAG, "LVGL Camera Display initialise (not started yet)");
  ESP_LOGI(TAG, "   Camera: Operationnelle");
  ESP_LOGI(TAG, "   Frame callback registered with streaming task");
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

    // Release currently displayed buffer before stopping
    if (this->displayed_buffer_ != nullptr && this->camera_ != nullptr) {
      this->camera_->release_buffer(this->displayed_buffer_);
      this->displayed_buffer_ = nullptr;
    }

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

// Frame callback (called from streaming task on Core 1)
void LVGLCameraDisplay::on_frame_callback_(uint8_t* buffer, uint32_t size, uint32_t index) {
  // Simply signal that a new frame is ready
  // The actual canvas update happens in update_camera_frame_() on the main thread
  this->frame_ready_ = true;
}

// Mise a jour de la frame camera (appelee par le timer LVGL)
void LVGLCameraDisplay::update_camera_frame_() {
  // Si la camera est en streaming, capturer ET mettre a jour le canvas
  if (!this->camera_->is_streaming()) {
    return;
  }

  // Check if a new frame is ready (signaled by callback from streaming task)
  if (!this->frame_ready_) {
    return;
  }

  // Clear flag
  this->frame_ready_ = false;

  uint32_t t1 = millis();
  this->update_canvas_();
  uint32_t t2 = millis();
  this->frame_count_++;

  // Accumuler les temps pour statistiques
  static uint32_t last_time = 0;
  static uint32_t total_canvas_ms = 0;

  total_canvas_ms += (t2 - t1);

  // Logger performance toutes les 100 frames
  if (this->frame_count_ % 100 == 0) {
    uint32_t now_time = millis();

    if (last_time > 0) {
      float elapsed = (now_time - last_time) / 1000.0f;  // secondes
      float fps = 100.0f / elapsed;
      float avg_canvas = total_canvas_ms / 100.0f;
      ESP_LOGI(TAG, "%u frames - FPS: %.2f | canvas: %.1fms",
               this->frame_count_, fps, avg_canvas);
    }
    last_time = now_time;
    total_canvas_ms = 0;
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

  lv_canvas_set_buffer(this->canvas_obj_, img_data, width, height, LV_COLOR_FORMAT_RGB565);
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
