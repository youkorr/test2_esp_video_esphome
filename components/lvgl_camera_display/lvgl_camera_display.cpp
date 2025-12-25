#include "lvgl_camera_display.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

// PPA hardware transform includes (C headers)
extern "C" {
#include "driver/ppa.h"  // Pixel-Processing Accelerator
#include "esp_heap_caps.h"
}

// Conditionally include face_detection only if it exists
#ifdef USE_FACE_DETECTION
#include "esphome/components/face_detection/face_detection.h"
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

  // Initialize PPA if enabled
  if (this->ppa_enabled_) {
    if (!this->init_ppa_()) {
      ESP_LOGE(TAG, "Failed to initialize PPA");
      this->mark_failed();
      return;
    }
  }

  // Log transformation settings
  if (this->rotation_ != 0 || this->mirror_x_ || this->mirror_y_) {
    const char *transform_mode = this->ppa_enabled_ ? "PPA hardware" : "LVGL software";
    ESP_LOGI(TAG, "   Transformations (via %s):", transform_mode);
    if (this->rotation_ != 0) {
      ESP_LOGI(TAG, "      Rotation: %d°", this->rotation_);
    }
    if (this->mirror_x_) {
      ESP_LOGI(TAG, "      Mirror X: enabled");
    }
    if (this->mirror_y_) {
      ESP_LOGI(TAG, "      Mirror Y: enabled");
    }
  }

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

    // Cleanup PPA if it was initialized
    if (this->ppa_enabled_) {
      this->cleanup_ppa_();
    }

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
  uint16_t width = this->camera_->get_image_width();
  uint16_t height = this->camera_->get_image_height();

  if (img_data == nullptr) {
    return;
  }

  // Optional: draw face detection results if configured
#ifdef USE_FACE_DETECTION
  if (this->face_detection_ != nullptr) {
    this->face_detection_->draw_on_frame(img_data, width, height);
  }
#endif

  // Apply PPA transformation if enabled
  uint8_t *display_data = img_data;
  if (this->ppa_enabled_ && (this->rotation_ != 0 || this->mirror_x_ || this->mirror_y_)) {
    if (!this->apply_ppa_transform_(img_data, width, height, &display_data)) {
      ESP_LOGW(TAG, "PPA transform failed, using original buffer");
      display_data = img_data;
    }
  }

  if (this->first_update_) {
    ESP_LOGI(TAG, "Premier update canvas (buffer pool):");
    ESP_LOGI(TAG, "   Dimensions: %ux%u", width, height);
    ESP_LOGI(TAG, "   Buffer: %p (index=%u)", img_data, this->camera_->get_buffer_index(buffer));
    ESP_LOGI(TAG, "   Premiers pixels (RGB565): %02X%02X %02X%02X %02X%02X",
             display_data[0], display_data[1], display_data[2], display_data[3], display_data[4], display_data[5]);
    if (this->ppa_enabled_) {
      ESP_LOGI(TAG, "   PPA transformation: enabled");
    }
    this->first_update_ = false;
  }

  lv_canvas_set_buffer(this->canvas_obj_, display_data, width, height, LV_IMG_CF_TRUE_COLOR);
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

    // Apply LVGL transformations (rotation and mirroring)
    if (this->rotation_ != 0 || this->mirror_x_ || this->mirror_y_) {
      ESP_LOGI(TAG, "   Applying LVGL transformations:");

      // Apply rotation
      if (this->rotation_ != 0) {
        int16_t angle = (this->rotation_ * 10);  // LVGL uses 0.1 degree units
        lv_obj_set_style_transform_angle(canvas, angle, 0);

        // Set pivot point to center for rotation
        lv_obj_set_style_transform_pivot_x(canvas, w / 2, 0);
        lv_obj_set_style_transform_pivot_y(canvas, h / 2, 0);

        ESP_LOGI(TAG, "      Rotation: %d°", this->rotation_);
      }

      // Apply mirroring using LVGL transform
      if (this->mirror_x_ || this->mirror_y_) {
        // Note: LVGL doesn't have direct mirror, but we can use negative scale
        // For now, log a warning - mirror will need pixel-level transformation
        if (this->mirror_x_) {
          ESP_LOGW(TAG, "      Mirror X: Not yet implemented in LVGL transform");
          ESP_LOGW(TAG, "      → Use rotation instead, or implement pixel transformation");
        }
        if (this->mirror_y_) {
          ESP_LOGW(TAG, "      Mirror Y: Not yet implemented in LVGL transform");
          ESP_LOGW(TAG, "      → Use rotation instead, or implement pixel transformation");
        }
      }
    }
  }
}

// ============================================================================
// PPA (Pixel-Processing Accelerator) Hardware Transform Functions
// ============================================================================

bool LVGLCameraDisplay::init_ppa_() {
  // Register PPA client
  ppa_client_config_t ppa_config = {
    .oper_type = PPA_OPERATION_SRM,
  };

  esp_err_t ret = ppa_register_client(&ppa_config, (ppa_client_handle_t *)&this->ppa_client_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register PPA client: %s", esp_err_to_name(ret));
    return false;
  }

  // Allocate PPA output buffer in SPIRAM (same size as input)
  uint16_t width = this->camera_->get_image_width();
  uint16_t height = this->camera_->get_image_height();
  size_t buffer_size = width * height * 2;  // RGB565

  this->ppa_buffer_ = (uint8_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
  if (this->ppa_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate PPA buffer (%u bytes)", buffer_size);
    ppa_unregister_client((ppa_client_handle_t)this->ppa_client_handle_);
    this->ppa_client_handle_ = nullptr;
    return false;
  }

  ESP_LOGI(TAG, "✓ PPA hardware transform initialized");
  ESP_LOGI(TAG, "   Rotation: %d°", this->rotation_);
  ESP_LOGI(TAG, "   Mirror X: %d, Mirror Y: %d", this->mirror_x_, this->mirror_y_);
  ESP_LOGI(TAG, "   Buffer: %u bytes @ %p", buffer_size, this->ppa_buffer_);

  return true;
}

bool LVGLCameraDisplay::apply_ppa_transform_(uint8_t *src_buffer, uint16_t width, uint16_t height, uint8_t **out_buffer) {
  if (!this->ppa_enabled_ || !this->ppa_client_handle_) {
    return true;  // No transformation
  }

  // Configure PPA transformation
  ppa_srm_oper_config_t srm_config = {};

  // Input buffer
  srm_config.in.buffer = src_buffer;
  srm_config.in.pic_w = width;
  srm_config.in.pic_h = height;
  srm_config.in.block_w = width;
  srm_config.in.block_h = height;
  srm_config.in.block_offset_x = 0;
  srm_config.in.block_offset_y = 0;
  srm_config.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

  // Output buffer
  srm_config.out.buffer = this->ppa_buffer_;
  srm_config.out.buffer_size = width * height * 2;
  srm_config.out.pic_w = width;
  srm_config.out.pic_h = height;
  srm_config.out.block_offset_x = 0;
  srm_config.out.block_offset_y = 0;
  srm_config.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

  // Transformation parameters
  srm_config.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
  if (this->rotation_ == 90) {
    srm_config.rotation_angle = PPA_SRM_ROTATION_ANGLE_90;
  } else if (this->rotation_ == 180) {
    srm_config.rotation_angle = PPA_SRM_ROTATION_ANGLE_180;
  } else if (this->rotation_ == 270) {
    srm_config.rotation_angle = PPA_SRM_ROTATION_ANGLE_270;
  }

  srm_config.scale_x = 1.0f;
  srm_config.scale_y = 1.0f;
  srm_config.mirror_x = this->mirror_x_;
  srm_config.mirror_y = this->mirror_y_;

  srm_config.rgb_swap = PPA_SRM_COLOR_RGB_SWAP_NONE;
  srm_config.byte_swap = false;
  srm_config.mode = PPA_TRANS_MODE_BLOCKING;

  // Execute PPA transformation
  esp_err_t ret = ppa_do_scale_rotate_mirror(
      (ppa_client_handle_t)this->ppa_client_handle_,
      &srm_config
  );

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "PPA transform failed: %s", esp_err_to_name(ret));
    return false;
  }

  // Return transformed buffer
  *out_buffer = this->ppa_buffer_;
  return true;
}

void LVGLCameraDisplay::cleanup_ppa_() {
  if (this->ppa_buffer_ != nullptr) {
    heap_caps_free(this->ppa_buffer_);
    this->ppa_buffer_ = nullptr;
  }

  if (this->ppa_client_handle_ != nullptr) {
    ppa_unregister_client((ppa_client_handle_t)this->ppa_client_handle_);
    this->ppa_client_handle_ = nullptr;
    ESP_LOGI(TAG, "✓ PPA hardware transform cleanup");
  }
}

}  // namespace lvgl_camera_display
}  // namespace esphome
