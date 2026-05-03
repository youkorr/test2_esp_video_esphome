#include "lvgl_camera_display.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <cstring>
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "driver/ppa.h"
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

  if (!this->camera_->is_pipeline_ready()) {
    ESP_LOGE(TAG, "Camera non operationnelle - pipeline non demarre");
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "LVGL Camera Display initialise (not started yet)");
  ESP_LOGI(TAG, "   Camera: Operationnelle");
  ESP_LOGI(TAG, "   Update interval: %u ms (~%d FPS) via LVGL timer",
           this->update_interval_, 1000 / this->update_interval_);
  ESP_LOGI(TAG, "   Mirror: x=%s y=%s | Rotation: %d°%s",
           this->mirror_x_ ? "on" : "off",
           this->mirror_y_ ? "on" : "off",
           this->rotation_,
           this->force_ppa_ ? " | PPA: forced" : "");
  ESP_LOGI(TAG, "Turn on the 'LVGL Camera Display' switch to start");
}

void LVGLCameraDisplay::loop() {
  if (this->enabled_ && this->lvgl_timer_ == nullptr) {
    ESP_LOGI(TAG, "Starting LVGL Camera Display...");
    this->lvgl_timer_ = lv_timer_create(lvgl_timer_callback_, this->update_interval_, this);
    if (this->lvgl_timer_ == nullptr) {
      ESP_LOGE(TAG, "Failed to create LVGL timer");
    } else {
      ESP_LOGI(TAG, "LVGL Camera Display started");
    }
  }

  if (!this->enabled_ && this->lvgl_timer_ != nullptr) {
    ESP_LOGI(TAG, "Stopping LVGL Camera Display...");
    lv_timer_del(this->lvgl_timer_);
    this->lvgl_timer_ = nullptr;
    ESP_LOGI(TAG, "LVGL Camera Display stopped");
  }
}

void LVGLCameraDisplay::lvgl_timer_callback_(lv_timer_t *timer) {
  LVGLCameraDisplay *display = static_cast<LVGLCameraDisplay *>(lv_timer_get_user_data(timer));
  if (display != nullptr) {
    display->update_camera_frame_();
  }
}

void LVGLCameraDisplay::update_camera_frame_() {
  if (!this->camera_->is_streaming()) {
    return;
  }

  uint32_t frame_start = millis();

  static uint32_t last_frame_start = 0;
  uint32_t frame_interval = 0;
  if (last_frame_start > 0) {
    frame_interval = frame_start - last_frame_start;
  }
  last_frame_start = frame_start;

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

  uint32_t frame_end = millis();
  uint32_t frame_cpu_time = frame_end - frame_start;

  static uint32_t last_stats_time = 0;
  static uint32_t total_capture_ms = 0;
  static uint32_t total_canvas_ms = 0;
  static uint32_t total_cpu_time_ms = 0;
  static uint32_t total_frame_interval_ms = 0;
  static uint32_t frame_interval_count = 0;

  total_capture_ms += (t2 - t1);
  total_canvas_ms += (t3 - t2);
  total_cpu_time_ms += frame_cpu_time;
  if (frame_interval > 0) {
    total_frame_interval_ms += frame_interval;
    frame_interval_count++;
  }

  if (this->frame_count_ % 100 == 0) {
    uint32_t now_time = millis();
    if (last_stats_time > 0 && frame_interval_count > 0) {
      float elapsed = (now_time - last_stats_time) / 1000.0f;
      float fps = 100.0f / elapsed;
      float avg_capture = total_capture_ms / 100.0f;
      float avg_canvas = total_canvas_ms / 100.0f;
      float avg_cpu_time = total_cpu_time_ms / 100.0f;
      float avg_frame_interval = total_frame_interval_ms / (float)frame_interval_count;
      float skip_rate = (skipped * 100.0f) / attempts;
      float cpu_percent = (avg_cpu_time / avg_frame_interval) * 100.0f;
      float lvgl_overhead = avg_frame_interval - avg_cpu_time;

      ESP_LOGI(TAG, "=== BENCHMARK (100 frames) === path=%s",
               this->ppa_path_active_ ? "PPA" : "zero-copy");
      ESP_LOGI(TAG, "  FPS: %.1f | CPU: %.1f%%", fps, cpu_percent);
      ESP_LOGI(TAG, "  Frame interval: %.1fms (target: %ums)", avg_frame_interval, this->update_interval_);
      ESP_LOGI(TAG, "  CPU time: %.1fms (capture: %.1fms + canvas: %.1fms)", avg_cpu_time, avg_capture, avg_canvas);
      ESP_LOGI(TAG, "  LVGL overhead: %.1fms | Skip: %.1f%%", lvgl_overhead, skip_rate);

      this->stats_fps_ = fps;
      this->stats_cpu_percent_ = cpu_percent;
      this->stats_frame_time_ = avg_cpu_time;
      this->stats_lvgl_overhead_ = lvgl_overhead;

      this->update_stats_label_();
    }
    last_stats_time = now_time;
    total_capture_ms = 0;
    total_canvas_ms = 0;
    total_cpu_time_ms = 0;
    total_frame_interval_ms = 0;
    frame_interval_count = 0;
    attempts = 0;
    skipped = 0;
  }
}

void LVGLCameraDisplay::update_stats_label_() {
  if (this->stats_label_ == nullptr) {
    return;
  }
  char buf[64];
  snprintf(buf, sizeof(buf), "FPS: %.1f | CPU: %.1f%%",
           this->stats_fps_, this->stats_cpu_percent_);
  lv_label_set_text(this->stats_label_, buf);
}

void LVGLCameraDisplay::set_stats_label(lv_obj_t *label) {
  this->stats_label_ = label;
  if (label != nullptr) {
    lv_label_set_text(label, "FPS: -- | CPU: --%");
    ESP_LOGI(TAG, "Stats label configured: %p", label);
  }
}

void LVGLCameraDisplay::dump_config() {
  ESP_LOGCONFIG(TAG, "LVGL Camera Display:");
  ESP_LOGCONFIG(TAG, "  Update interval: %u ms", this->update_interval_);
  ESP_LOGCONFIG(TAG, "  FPS cible: ~%d", 1000 / this->update_interval_);
  ESP_LOGCONFIG(TAG, "  Canvas configure: %s", this->canvas_obj_ ? "OUI" : "NON");
  ESP_LOGCONFIG(TAG, "  PPA path: %s", this->ppa_path_active_ ? "active" : "zero-copy");
  ESP_LOGCONFIG(TAG, "  Mirror: x=%s y=%s | Rotation: %d°",
                this->mirror_x_ ? "on" : "off",
                this->mirror_y_ ? "on" : "off",
                this->rotation_);
}

bool LVGLCameraDisplay::ensure_ppa_(uint16_t out_w, uint16_t out_h) {
  // Re-allocate if output size changed.
  size_t needed = (size_t)out_w * (size_t)out_h * 2;  // RGB565
  if (this->ppa_inited_ && (this->ppa_out_w_ != out_w || this->ppa_out_h_ != out_h)) {
    if (this->ppa_out_buf_) {
      heap_caps_free(this->ppa_out_buf_);
      this->ppa_out_buf_ = nullptr;
      this->ppa_out_buf_size_ = 0;
    }
  }

  if (!this->ppa_handle_) {
    ppa_client_config_t cfg = {};
    cfg.oper_type = PPA_OPERATION_SRM;
    cfg.max_pending_trans_num = 1;
    ppa_client_handle_t handle = nullptr;
    esp_err_t err = ppa_register_client(&cfg, &handle);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "PPA client registration failed: %d", (int)err);
      return false;
    }
    this->ppa_handle_ = (void *)handle;
  }

  if (!this->ppa_out_buf_) {
    // PPA needs DMA-capable, 64-byte aligned PSRAM.
    this->ppa_out_buf_ = (uint8_t *)heap_caps_aligned_calloc(
        64, 1, needed, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
    if (!this->ppa_out_buf_) {
      ESP_LOGE(TAG, "PPA output buffer alloc failed (%zu bytes)", needed);
      return false;
    }
    this->ppa_out_buf_size_ = needed;
    this->ppa_out_w_ = out_w;
    this->ppa_out_h_ = out_h;
    ESP_LOGI(TAG, "PPA output buffer allocated: %ux%u (%zu bytes) @ %p",
             out_w, out_h, needed, this->ppa_out_buf_);
  }

  this->ppa_inited_ = true;
  return true;
}

void LVGLCameraDisplay::update_canvas_() {
  if (this->camera_ == nullptr) return;

  if (this->canvas_obj_ == nullptr) {
    if (!this->canvas_warning_shown_) {
      ESP_LOGW(TAG, "Canvas/Image null - pas encore configure?");
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
    return;
  }

  uint8_t *img_data = this->camera_->get_buffer_data(buffer);
  uint16_t cam_w = this->camera_->get_image_width();
  uint16_t cam_h = this->camera_->get_image_height();
  if (img_data == nullptr) {
    return;
  }

  // Detect widget type on first update
  if (this->first_update_) {
    this->is_canvas_ = lv_obj_check_type(this->canvas_obj_, &lv_canvas_class);
    ESP_LOGI(TAG, "Premier update - Widget type: %s", this->is_canvas_ ? "CANVAS" : "IMAGE");
    ESP_LOGI(TAG, "   Camera: %ux%u   Buffer: %p (index=%u)", cam_w, cam_h,
             img_data, this->camera_->get_buffer_index(buffer));
  }

  // Read canvas size on every frame (cheap - just two getters) so user
  // can resize widgets at runtime if needed.
  uint16_t canvas_w = (uint16_t)lv_obj_get_width(this->canvas_obj_);
  uint16_t canvas_h = (uint16_t)lv_obj_get_height(this->canvas_obj_);
  if (canvas_w == 0 || canvas_h == 0) {
    canvas_w = cam_w;
    canvas_h = cam_h;
  }

  // Decide path: zero-copy if no transform needed, PPA otherwise.
  bool transform_needed = this->force_ppa_ ||
                          this->mirror_x_ ||
                          this->mirror_y_ ||
                          this->rotation_ != 0 ||
                          (canvas_w != cam_w || canvas_h != cam_h);

  // For 90/270 rotation the output dims are swapped.
  uint16_t out_w = canvas_w;
  uint16_t out_h = canvas_h;
  if (this->rotation_ == 90 || this->rotation_ == 270) {
    // After rotation by 90/270, the canvas should display rotated pixels.
    // We let canvas_w/canvas_h drive the final geometry; user is expected
    // to set the canvas dims accordingly. The PPA `out` block matches.
  }

  bool ppa_ok = false;
  if (transform_needed) {
    ppa_ok = this->ensure_ppa_(out_w, out_h);
    if (!ppa_ok) {
      ESP_LOGW(TAG, "PPA setup failed - falling back to zero-copy");
      transform_needed = false;
    }
  }
  this->ppa_path_active_ = transform_needed && ppa_ok;

  // ---- detector overlay drawing happens on the *camera* buffer in both
  //      paths so detectors don't have to know about output geometry ----

  // Cache invalidate before CPU/DMA reads the camera buffer
  uint32_t cam_size = (uint32_t)cam_w * (uint32_t)cam_h * 2;
  esp_cache_msync(img_data, cam_size,
                  ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);

#ifdef USE_FACE_DETECTION
  if (this->face_detection_ != nullptr) {
    this->face_detection_->draw_on_frame(img_data, cam_w, cam_h);
  }
#endif
#ifdef USE_YOLO11_DETECTION
  if (this->yolo11_detection_ != nullptr) {
    this->yolo11_detection_->draw_on_frame(img_data, cam_w, cam_h);
  }
#endif
#ifdef USE_PEDESTRIAN_DETECTION
  if (this->pedestrian_detection_ != nullptr) {
    this->pedestrian_detection_->draw_on_frame(img_data, cam_w, cam_h);
  }
#endif

  // Cache flush so PPA / DMA reads the data we just wrote
  esp_cache_msync(img_data, cam_size,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);

  uint8_t *display_buf = img_data;
  uint16_t display_w = cam_w;
  uint16_t display_h = cam_h;

  if (this->ppa_path_active_) {
    // Run the SRM transform: camera buffer (RGB565) -> PPA output buffer (RGB565).
    ppa_srm_oper_config_t op = {};
    op.in.buffer        = img_data;
    op.in.pic_w         = cam_w;
    op.in.pic_h         = cam_h;
    op.in.block_w       = cam_w;
    op.in.block_h       = cam_h;
    op.in.block_offset_x = 0;
    op.in.block_offset_y = 0;
    op.in.srm_cm        = PPA_SRM_COLOR_MODE_RGB565;

    op.out.buffer       = this->ppa_out_buf_;
    op.out.buffer_size  = this->ppa_out_buf_size_;
    op.out.pic_w        = out_w;
    op.out.pic_h        = out_h;
    op.out.block_offset_x = 0;
    op.out.block_offset_y = 0;
    op.out.srm_cm       = PPA_SRM_COLOR_MODE_RGB565;

    switch (this->rotation_) {
      case 90:  op.rotation_angle = PPA_SRM_ROTATION_ANGLE_90;  break;
      case 180: op.rotation_angle = PPA_SRM_ROTATION_ANGLE_180; break;
      case 270: op.rotation_angle = PPA_SRM_ROTATION_ANGLE_270; break;
      default:  op.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;   break;
    }
    // Compute scale so the input fully fills the output. For non-90/270
    // rotation that is a straightforward ratio.
    if (this->rotation_ == 90 || this->rotation_ == 270) {
      op.scale_x = (float)out_h / (float)cam_w;
      op.scale_y = (float)out_w / (float)cam_h;
    } else {
      op.scale_x = (float)out_w / (float)cam_w;
      op.scale_y = (float)out_h / (float)cam_h;
    }
    op.mirror_x  = this->mirror_x_;
    op.mirror_y  = this->mirror_y_;
    op.rgb_swap  = false;
    op.byte_swap = false;
    op.mode      = PPA_TRANS_MODE_BLOCKING;

    esp_err_t err = ppa_do_scale_rotate_mirror(
        (ppa_client_handle_t)this->ppa_handle_, &op);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "ppa_do_scale_rotate_mirror failed: %d - using camera buffer directly", (int)err);
    } else {
      display_buf = this->ppa_out_buf_;
      display_w = out_w;
      display_h = out_h;
      if (!this->ppa_logged_) {
        ESP_LOGI(TAG, "PPA active: %ux%u -> %ux%u (mirror x=%d y=%d, rot=%d°, scale=%.2f/%.2f)",
                 cam_w, cam_h, out_w, out_h,
                 (int)op.mirror_x, (int)op.mirror_y, this->rotation_,
                 (double)op.scale_x, (double)op.scale_y);
        this->ppa_logged_ = true;
      }
    }
  }

  // ---- hand the chosen buffer to LVGL ----
  uint32_t stride = display_w * 2;
  uint32_t buf_size = (uint32_t)display_w * (uint32_t)display_h * 2;

  if (!this->draw_buf_initialized_) {
    lv_draw_buf_init(&this->camera_draw_buf_, display_w, display_h,
                     LV_COLOR_FORMAT_RGB565, stride, display_buf, buf_size);
    lv_draw_buf_set_flag(&this->camera_draw_buf_, LV_IMAGE_FLAGS_MODIFIABLE);
    this->draw_buf_initialized_ = true;
    ESP_LOGI(TAG, "draw_buf init: %ux%u stride=%u size=%u data=%p (path=%s)",
             display_w, display_h, stride, buf_size, display_buf,
             this->ppa_path_active_ ? "PPA" : "zero-copy");
  } else {
    // If the displayed dims changed (e.g. canvas was resized), re-init
    // the draw buf so LVGL picks up the new geometry.
    if (this->camera_draw_buf_.header.w != display_w ||
        this->camera_draw_buf_.header.h != display_h) {
      lv_draw_buf_init(&this->camera_draw_buf_, display_w, display_h,
                       LV_COLOR_FORMAT_RGB565, stride, display_buf, buf_size);
      lv_draw_buf_set_flag(&this->camera_draw_buf_, LV_IMAGE_FLAGS_MODIFIABLE);
      ESP_LOGI(TAG, "draw_buf reinit (resize): %ux%u (path=%s)",
               display_w, display_h,
               this->ppa_path_active_ ? "PPA" : "zero-copy");
    } else {
      this->camera_draw_buf_.data = display_buf;
    }
  }

  if (this->is_canvas_) {
    lv_canvas_set_draw_buf(this->canvas_obj_, &this->camera_draw_buf_);
  } else {
    lv_image_set_src(this->canvas_obj_, &this->camera_draw_buf_);
  }

  lv_obj_invalidate(this->canvas_obj_);

  this->first_update_ = false;

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

  // Force re-init of the draw buf next frame so the chosen path is
  // re-evaluated against the new canvas size.
  this->draw_buf_initialized_ = false;
  this->ppa_logged_ = false;
}

}  // namespace lvgl_camera_display
}  // namespace esphome
