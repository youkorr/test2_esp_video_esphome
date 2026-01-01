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

// ============================================================================
// IMLIB Integration - Drawing on camera frames
// ============================================================================
#if ENABLE_IMLIB_DRAWING && __has_include("imlib.h")
  extern "C" {
    #include "imlib.h"
  }
  #define IMLIB_DRAWING_ENABLED 1
#else
  #define IMLIB_DRAWING_ENABLED 0
#endif

namespace esphome {
namespace lvgl_camera_display {

static const char *const TAG = "lvgl_camera_display";

// ============================================================================
// Execute Imlib Drawing Commands
// ============================================================================
#if IMLIB_DRAWING_ENABLED
static void execute_draw_commands(const std::vector<esp_cam_sensor::DrawCommand>& commands,
                                   uint8_t* buffer, uint16_t width, uint16_t height) {
  if (commands.empty()) {
    return;
  }

  // Wrap buffer as imlib image_t (RGB565)
  image_t img;
  img.w = width;
  img.h = height;
  img.pixfmt = PIXFORMAT_RGB565;
  img.data = buffer;

  // Execute each command
  for (const auto& cmd : commands) {
    switch (cmd.type) {
      case esp_cam_sensor::DrawCommandType::STRING:
        imlib_draw_string(&img, cmd.x, cmd.y, cmd.string_params.text, cmd.color,
                          cmd.string_params.scale, 1, 1, 0, false, false, PIXFORMAT_RGB565, nullptr);
        break;

      case esp_cam_sensor::DrawCommandType::LINE:
        imlib_draw_line(&img, cmd.x, cmd.y, cmd.line_params.x1, cmd.line_params.y1,
                        cmd.color, cmd.line_params.thickness);
        break;

      case esp_cam_sensor::DrawCommandType::RECTANGLE:
        imlib_draw_rectangle(&img, cmd.x, cmd.y, cmd.rect_params.w, cmd.rect_params.h,
                             cmd.color, cmd.rect_params.thickness, cmd.rect_params.fill);
        break;

      case esp_cam_sensor::DrawCommandType::CIRCLE:
        imlib_draw_circle(&img, cmd.x, cmd.y, cmd.circle_params.radius,
                          cmd.color, cmd.circle_params.thickness, cmd.circle_params.fill);
        break;

      case esp_cam_sensor::DrawCommandType::PIXEL:
        imlib_set_pixel(&img, cmd.x, cmd.y, cmd.color);
        break;
    }
  }
}
#else
// Stub when imlib not available
static void execute_draw_commands(const std::vector<esp_cam_sensor::DrawCommand>& commands,
                                   uint8_t* buffer, uint16_t width, uint16_t height) {
  static bool warning_shown = false;
  if (!commands.empty() && !warning_shown) {
    ESP_LOGW(TAG, "imlib drawing commands queued but imlib is not available");
    ESP_LOGW(TAG, "Compile with -DENABLE_IMLIB_DRAWING and ensure imlib component is loaded");
    warning_shown = true;
  }
}
#endif

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
#if IMLIB_DRAWING_ENABLED
  ESP_LOGI(TAG, "   imlib drawing: ENABLED (zero-copy overlay on video frames)");
#else
  ESP_LOGI(TAG, "   imlib drawing: DISABLED (compile with -DENABLE_IMLIB_DRAWING to enable)");
#endif
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
  LVGLCameraDisplay *display = static_cast<LVGLCameraDisplay *>(timer->user_data);
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
  uint16_t img_width = this->camera_->get_image_width();
  uint16_t img_height = this->camera_->get_image_height();

  if (img_data == nullptr) {
    return;
  }

  // Get canvas dimensions from LVGL widget (NOT from camera!)
  lv_coord_t canvas_width = lv_obj_get_width(this->canvas_obj_);
  lv_coord_t canvas_height = lv_obj_get_height(this->canvas_obj_);

  // Optional: draw detection results if configured
#ifdef USE_FACE_DETECTION
  if (this->face_detection_ != nullptr) {
    this->face_detection_->draw_on_frame(img_data, img_width, img_height);
  }
#endif
#ifdef USE_YOLO11_DETECTION
  if (this->yolo11_detection_ != nullptr) {
    this->yolo11_detection_->draw_on_frame(img_data, img_width, img_height);
  }
#endif
#ifdef USE_PEDESTRIAN_DETECTION
  if (this->pedestrian_detection_ != nullptr) {
    this->pedestrian_detection_->draw_on_frame(img_data, img_width, img_height);
  }
#endif

  // ============================================================================
  // Execute Imlib Drawing Commands (from camera component queue)
  // ============================================================================
  // Get and execute all queued drawing commands from the camera component
  const auto& draw_commands = this->camera_->get_pending_draw_commands();
  if (!draw_commands.empty()) {
    execute_draw_commands(draw_commands, img_data, img_width, img_height);
    this->camera_->clear_draw_commands();  // Clear queue after execution
  }

  if (this->first_update_) {
    ESP_LOGI(TAG, "Premier update canvas (buffer pool):");
    ESP_LOGI(TAG, "   Image: %ux%u", img_width, img_height);
    ESP_LOGI(TAG, "   Canvas: %dx%d", canvas_width, canvas_height);
    ESP_LOGI(TAG, "   Buffer: %p (index=%u)", img_data, this->camera_->get_buffer_index(buffer));
    ESP_LOGI(TAG, "   Premiers pixels (RGB565): %02X%02X %02X%02X %02X%02X",
             img_data[0], img_data[1], img_data[2], img_data[3], img_data[4], img_data[5]);

    // CRITICAL WARNING if sizes don't match
    if (img_width != canvas_width || img_height != canvas_height) {
      ESP_LOGW(TAG, "⚠️  SIZE MISMATCH! Image=%ux%u but Canvas=%dx%d",
               img_width, img_height, canvas_width, canvas_height);
      ESP_LOGW(TAG, "⚠️  This will cause slow LVGL software resize!");
      ESP_LOGW(TAG, "⚠️  Solution: Enable PPA with output_width=%d output_height=%d",
               canvas_width, canvas_height);
    }

    this->first_update_ = false;
  }

  // IMPORTANT: Use CANVAS size, not image size!
  // This ensures buffer dimensions match canvas widget dimensions
  lv_canvas_set_buffer(this->canvas_obj_, img_data, canvas_width, canvas_height, LV_IMG_CF_TRUE_COLOR);
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
