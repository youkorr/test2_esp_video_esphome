#include "lvgl_camera_display.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <cstring>
#include "esp_cache.h"
#ifdef CONFIG_IDF_TARGET_ESP32P4
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"  // esp_cache_get_alignment() - PPA requires the
                                            // output buffer addr AND size to be aligned to
                                            // the SPIRAM cache line (128B on L2_CACHE_LINE_128B).
#endif
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

  // Temps de debut de cette frame
  uint32_t frame_start = millis();

  // Mesurer l'intervalle entre frames (temps total depuis la derniere frame)
  static uint32_t last_frame_start = 0;
  uint32_t frame_interval = 0;
  if (last_frame_start > 0) {
    frame_interval = frame_start - last_frame_start;
  }
  last_frame_start = frame_start;

  // Statistiques de frames manquees
  static uint32_t attempts = 0;
  static uint32_t skipped = 0;

  uint32_t t1 = millis();
  bool frame_captured = true;
#ifdef CONFIG_IDF_TARGET_ESP32P4
  // When the async PPA pipeline is running, the producer task does all the
  // V4L2 capture. The LVGL callback only consumes already-resized buffers
  // from the queue and must NOT call camera_->capture_frame() (would race
  // with the producer for V4L2 buffers).
  if (!this->ppa_async_enabled_) {
    frame_captured = this->camera_->capture_frame();
  }
#else
  frame_captured = this->camera_->capture_frame();
#endif
  uint32_t t2 = millis();

  attempts++;
  if (!frame_captured) {
    skipped++;
    return;
  }

  this->update_canvas_();
  uint32_t t3 = millis();
  this->frame_count_++;

  // Temps de fin - mesure le temps CPU utilise pour cette frame
  uint32_t frame_end = millis();
  uint32_t frame_cpu_time = frame_end - frame_start;

  // Accumuler les temps pour statistiques
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

  // Logger performance toutes les 100 frames
  if (this->frame_count_ % 100 == 0) {
    uint32_t now_time = millis();

    if (last_stats_time > 0 && frame_interval_count > 0) {
      float elapsed = (now_time - last_stats_time) / 1000.0f;  // secondes
      float fps = 100.0f / elapsed;
      float avg_capture = total_capture_ms / 100.0f;
      float avg_canvas = total_canvas_ms / 100.0f;
      float avg_cpu_time = total_cpu_time_ms / 100.0f;
      float avg_frame_interval = total_frame_interval_ms / (float)frame_interval_count;
      float skip_rate = (skipped * 100.0f) / attempts;

      // CPU % = temps CPU utilise / temps total disponible * 100
      float cpu_percent = (avg_cpu_time / avg_frame_interval) * 100.0f;

      // Temps "perdu" = intervalle - temps CPU (temps ou LVGL fait autre chose)
      float lvgl_overhead = avg_frame_interval - avg_cpu_time;

      ESP_LOGI(TAG, "=== BENCHMARK (100 frames) ===");
      ESP_LOGI(TAG, "  FPS: %.1f | CPU: %.1f%%", fps, cpu_percent);
      ESP_LOGI(TAG, "  Frame interval: %.1fms (target: %ums)", avg_frame_interval, this->update_interval_);
      ESP_LOGI(TAG, "  CPU time: %.1fms (capture: %.1fms + canvas: %.1fms)", avg_cpu_time, avg_capture, avg_canvas);
      ESP_LOGI(TAG, "  LVGL overhead: %.1fms | Skip: %.1f%%", lvgl_overhead, skip_rate);

      // Mettre a jour les stats pour l'affichage UI
      this->stats_fps_ = fps;
      this->stats_cpu_percent_ = cpu_percent;
      this->stats_frame_time_ = avg_cpu_time;
      this->stats_lvgl_overhead_ = lvgl_overhead;

      // Mettre a jour le label de stats si configure
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

  // Format: "FPS: 12.5 | CPU: 8.5%"
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
}

void LVGLCameraDisplay::update_canvas_() {
  if (this->camera_ == nullptr) {
    return;
  }

  if (this->canvas_obj_ == nullptr) {
    if (!this->canvas_warning_shown_) {
      ESP_LOGW(TAG, "Canvas/Image null - pas encore configure?");
      this->canvas_warning_shown_ = true;
    }
    return;
  }

#ifdef CONFIG_IDF_TARGET_ESP32P4
  // First-update path: attempt to start the async PPA producer if the canvas
  // dimensions don't match the camera. Done before any buffer acquisition so
  // the producer owns V4L2 acquisitions exclusively from frame #1 onwards.
  if (!this->ppa_setup_attempted_ && this->camera_->is_streaming()) {
    lv_coord_t canvas_w = lv_obj_get_width(this->canvas_obj_);
    lv_coord_t canvas_h = lv_obj_get_height(this->canvas_obj_);
    uint16_t cam_w = this->camera_->get_image_width();
    uint16_t cam_h = this->camera_->get_image_height();
    if (canvas_w > 0 && canvas_h > 0 && cam_w > 0 && cam_h > 0) {
      this->ppa_setup_attempted_ = true;
      this->is_canvas_ = lv_obj_check_type(this->canvas_obj_, &lv_canvas_class);
      if ((uint16_t)canvas_w != cam_w || (uint16_t)canvas_h != cam_h) {
        ESP_LOGI(TAG, "Canvas %dx%d != camera %ux%u - starting async PPA pipeline",
                 canvas_w, canvas_h, cam_w, cam_h);
        this->setup_ppa_async_pipeline_(cam_w, cam_h, (uint16_t)canvas_w, (uint16_t)canvas_h);
      } else {
        ESP_LOGI(TAG, "Canvas size matches camera (%ux%u) - sync zero-copy path", cam_w, cam_h);
      }
    }
  }

  // === ASYNC PATH ===
  // The producer task fills filled_queue_ with display-sized buffers.
  // Drain everything available, keep only the most recent, and return the
  // older ones to free_queue_. If nothing new arrived, keep showing the
  // previous buffer (LVGL won't redraw if data pointer is unchanged).
  if (this->ppa_async_enabled_) {
    uint8_t *new_buf = nullptr;
    bool got_new = false;
    while (xQueueReceive(this->filled_queue_, &new_buf, 0) == pdTRUE) {
      if (this->async_displayed_buf_ != nullptr) {
        xQueueSend(this->free_queue_, &this->async_displayed_buf_, 0);
      }
      this->async_displayed_buf_ = new_buf;
      got_new = true;
    }
    if (this->async_displayed_buf_ == nullptr) {
      return;  // producer hasn't produced anything yet
    }
    uint8_t *img_data = this->async_displayed_buf_;
    uint16_t width = this->display_buf_w_;
    uint16_t height = this->display_buf_h_;
    uint32_t stride = (uint32_t)width * 2;
    uint32_t buf_size = (uint32_t)width * height * 2;

    if (!this->draw_buf_initialized_) {
      lv_draw_buf_init(&this->camera_draw_buf_, width, height,
                       LV_COLOR_FORMAT_RGB565, stride, img_data, buf_size);
      lv_draw_buf_set_flag(&this->camera_draw_buf_, LV_IMAGE_FLAGS_MODIFIABLE);
      this->draw_buf_initialized_ = true;
      ESP_LOGI(TAG, "Zero-copy draw_buf initialized (async): %ux%u, stride=%u, data=%p",
               width, height, stride, img_data);
    } else if (got_new) {
      this->camera_draw_buf_.data = img_data;
    }
    if (got_new) {
      if (this->is_canvas_) {
        lv_canvas_set_draw_buf(this->canvas_obj_, &this->camera_draw_buf_);
      } else {
        lv_image_set_src(this->canvas_obj_, &this->camera_draw_buf_);
      }
      lv_obj_invalidate(this->canvas_obj_);
    }
    this->first_update_ = false;
    return;
  }
#endif  // CONFIG_IDF_TARGET_ESP32P4

  // === SYNC PATH === (canvas == camera size, or non-ESP32P4)
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

  // ESP32-P4: Invalidate CPU cache before reading PSRAM buffer filled by DMA.
  // Camera DMA writes to PSRAM but CPU cache may hold stale data for this address.
  uint32_t frame_size = width * height * 2;  // RGB565
  esp_cache_msync(img_data, frame_size,
                  ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);

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

  // ESP32-P4: Flush CPU cache to PSRAM after detection drawing.
  // Without this, PPA/DMA reads stale data from PSRAM (see LVGL PR #9162).
  uint32_t buf_size_bytes = width * height * 2;
  esp_cache_msync(img_data, buf_size_bytes,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);

  // Sync-path first-update log (only reached when canvas matches camera or
  // we're not on ESP32-P4). The async pipeline has its own first-update
  // log emitted by setup_ppa_async_pipeline_.
  if (this->first_update_) {
    this->is_canvas_ = lv_obj_check_type(this->canvas_obj_, &lv_canvas_class);
    ESP_LOGI(TAG, "Premier update (sync) - Widget type: %s", this->is_canvas_ ? "CANVAS" : "IMAGE");
    ESP_LOGI(TAG, "   Dimensions: %ux%u", width, height);
    ESP_LOGI(TAG, "   Buffer: %p (index=%u)", img_data, this->camera_->get_buffer_index(buffer));
  }

  // LVGL 9.4 ZERO-COPY MODE
  // Camera buffer stride = width * 2 (RGB565, no padding between rows)
  uint32_t stride = width * 2;
  uint32_t buf_size = width * height * 2;

  if (!this->draw_buf_initialized_) {
    // Initialize the draw buffer structure to point to camera data
    lv_draw_buf_init(&this->camera_draw_buf_, width, height,
                     LV_COLOR_FORMAT_RGB565, stride, img_data, buf_size);

    // Mark as modifiable so LVGL knows we'll update the data pointer
    lv_draw_buf_set_flag(&this->camera_draw_buf_, LV_IMAGE_FLAGS_MODIFIABLE);

    this->draw_buf_initialized_ = true;

    ESP_LOGI(TAG, "Zero-copy draw_buf initialized: %ux%u, stride=%u, size=%u, data=%p",
             width, height, stride, buf_size, img_data);
  } else {
    // Just update the data pointer - no memcpy needed!
    this->camera_draw_buf_.data = img_data;
  }

  // LVGL 9.4: Use the correct API depending on widget type
  if (this->is_canvas_) {
    // For canvas widgets: lv_canvas_set_draw_buf() properly updates
    // the canvas's internal draw_buf field AND calls lv_image_set_src()
    lv_canvas_set_draw_buf(this->canvas_obj_, &this->camera_draw_buf_);
  } else {
    // For image widgets: lv_image_set_src() is sufficient
    lv_image_set_src(this->canvas_obj_, &this->camera_draw_buf_);
  }

  // Force invalidation - LVGL 9.4 may skip redraw when same pointer is reused
  lv_obj_invalidate(this->canvas_obj_);

  this->first_update_ = false;

  // Tracker ce buffer pour le liberer au prochain update.
  // If PPA pre-resize ran, buffer is already nullptr (released immediately
  // after the PPA copy completed) and there is nothing to track.
  this->displayed_buffer_ = buffer;
}

#ifdef CONFIG_IDF_TARGET_ESP32P4

// Producer task: runs at high priority on core 1 (same arrangement as the
// Waveshare brookesia camera app). Owns the V4L2 buffer flow: pulls a
// camera frame, runs detection drawing, flushes cache, submits a PPA SRM
// scale in NON_BLOCKING mode, waits on a semaphore the PPA done callback
// gives, releases the camera buffer, and pushes the resized SPIRAM buffer
// to the filled queue. This decouples camera capture and PPA work from
// the LVGL render thread - any wait on shared PPA hardware (LVGL also
// uses it for display rotation) happens here, not in the LVGL timer
// callback.

void LVGLCameraDisplay::producer_task_entry_(void *arg) {
  static_cast<LVGLCameraDisplay *>(arg)->producer_loop_();
  vTaskDelete(nullptr);
}

bool LVGLCameraDisplay::ppa_trans_done_cb_(ppa_client_handle_t client,
                                            ppa_event_data_t *event_data,
                                            void *user_data) {
  auto *self = static_cast<LVGLCameraDisplay *>(user_data);
  BaseType_t hpw = pdFALSE;
  xSemaphoreGiveFromISR(self->ppa_done_sem_, &hpw);
  return hpw == pdTRUE;
}

bool LVGLCameraDisplay::submit_ppa_scale_(const uint8_t *src, uint16_t src_w,
                                           uint16_t src_h, uint8_t *dst) {
  ppa_in_pic_blk_config_t in = {};
  in.buffer = (void *)src;
  in.pic_w = src_w;
  in.pic_h = src_h;
  in.block_w = src_w;
  in.block_h = src_h;
  in.block_offset_x = 0;
  in.block_offset_y = 0;
  in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

  ppa_out_pic_blk_config_t out = {};
  out.buffer = dst;
  out.buffer_size = this->display_buf_size_;  // cache-line aligned
  out.pic_w = this->display_buf_w_;
  out.pic_h = this->display_buf_h_;
  out.block_offset_x = 0;
  out.block_offset_y = 0;
  out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

  ppa_srm_oper_config_t cfg = {};
  cfg.in = in;
  cfg.out = out;
  cfg.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
  cfg.scale_x = (float)this->display_buf_w_ / (float)src_w;
  cfg.scale_y = (float)this->display_buf_h_ / (float)src_h;
  cfg.mirror_x = false;
  cfg.mirror_y = false;
  cfg.rgb_swap = 0;
  cfg.byte_swap = 0;
  cfg.mode = PPA_TRANS_MODE_NON_BLOCKING;
  cfg.user_data = this;

  esp_err_t ret = ppa_do_scale_rotate_mirror(this->ppa_srm_client_, &cfg);
  if (ret != ESP_OK) {
    if (this->ppa_error_count_ < 5) {
      ESP_LOGW(TAG, "PPA scale submit failed: %s", esp_err_to_name(ret));
      this->ppa_error_count_++;
    }
    return false;
  }
  return true;
}

void LVGLCameraDisplay::producer_loop_() {
  ESP_LOGI(TAG, "PPA producer task started (core=%d, prio=%d)",
           xPortGetCoreID(), uxTaskPriorityGet(nullptr));

  while (this->producer_should_run_) {
    if (!this->camera_->is_streaming() || !this->enabled_) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    uint8_t *dst_buf = nullptr;
    if (xQueueReceive(this->free_queue_, &dst_buf, pdMS_TO_TICKS(50)) != pdTRUE) {
      // Consumer hasn't drained yet, retry
      continue;
    }

    if (!this->camera_->capture_frame()) {
      xQueueSend(this->free_queue_, &dst_buf, 0);
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }
    auto *cam_buf = this->camera_->acquire_buffer();
    if (cam_buf == nullptr) {
      xQueueSend(this->free_queue_, &dst_buf, 0);
      continue;
    }

    uint8_t *src = this->camera_->get_buffer_data(cam_buf);
    uint16_t src_w = this->camera_->get_image_width();
    uint16_t src_h = this->camera_->get_image_height();
    uint32_t src_size = (uint32_t)src_w * src_h * 2;

    // Cache invalidate after DMA filled the camera buffer.
    esp_cache_msync(src, src_size,
                    ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);

#ifdef USE_FACE_DETECTION
    if (this->face_detection_ != nullptr) {
      this->face_detection_->draw_on_frame(src, src_w, src_h);
    }
#endif
#ifdef USE_YOLO11_DETECTION
    if (this->yolo11_detection_ != nullptr) {
      this->yolo11_detection_->draw_on_frame(src, src_w, src_h);
    }
#endif
#ifdef USE_PEDESTRIAN_DETECTION
    if (this->pedestrian_detection_ != nullptr) {
      this->pedestrian_detection_->draw_on_frame(src, src_w, src_h);
    }
#endif

    // Flush cache before PPA reads.
    esp_cache_msync(src, src_size,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);

    if (!this->submit_ppa_scale_(src, src_w, src_h, dst_buf)) {
      this->camera_->release_buffer(cam_buf);
      xQueueSend(this->free_queue_, &dst_buf, 0);
      continue;
    }
    // Block ONLY this task on the PPA-done semaphore. LVGL's timer
    // callback keeps running on its own task without waiting.
    if (xSemaphoreTake(this->ppa_done_sem_, pdMS_TO_TICKS(100)) != pdTRUE) {
      ESP_LOGW(TAG, "PPA done semaphore timeout - dropping frame");
      this->camera_->release_buffer(cam_buf);
      xQueueSend(this->free_queue_, &dst_buf, 0);
      continue;
    }

    // Camera buffer no longer needed - PPA already copied it.
    this->camera_->release_buffer(cam_buf);

    // Invalidate display-buffer cache so the LVGL task reads fresh pixels.
    esp_cache_msync(dst_buf, this->display_buf_size_,
                    ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);

    // Hand it to the consumer.
    if (xQueueSend(this->filled_queue_, &dst_buf, 0) != pdTRUE) {
      // Should never happen (filled_queue_ capacity == NUM_DISPLAY_BUFS),
      // but if it does, return the buffer to the free pool.
      xQueueSend(this->free_queue_, &dst_buf, 0);
    }
  }

  ESP_LOGI(TAG, "PPA producer task exiting");
}

bool LVGLCameraDisplay::setup_ppa_async_pipeline_(uint16_t cam_w, uint16_t cam_h,
                                                   uint16_t canvas_w, uint16_t canvas_h) {
  if (this->ppa_async_enabled_) {
    return true;
  }

  ppa_client_config_t client_cfg = {};
  client_cfg.oper_type = PPA_OPERATION_SRM;
  client_cfg.max_pending_trans_num = 1;
  if (ppa_register_client(&client_cfg, &this->ppa_srm_client_) != ESP_OK) {
    ESP_LOGW(TAG, "ppa_register_client failed - async pipeline disabled, falling back to sync path");
    this->ppa_srm_client_ = nullptr;
    return false;
  }
  ppa_event_callbacks_t cbs = {};
  cbs.on_trans_done = &LVGLCameraDisplay::ppa_trans_done_cb_;
  if (ppa_client_register_event_callbacks(this->ppa_srm_client_, &cbs) != ESP_OK) {
    ESP_LOGW(TAG, "ppa_client_register_event_callbacks failed - async disabled");
    ppa_unregister_client(this->ppa_srm_client_);
    this->ppa_srm_client_ = nullptr;
    return false;
  }

  size_t cache_line = 64;
  esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &cache_line);
  if (cache_line < 64) cache_line = 64;
  size_t raw_size = (size_t)canvas_w * (size_t)canvas_h * 2;
  size_t aligned_size = (raw_size + cache_line - 1) & ~(cache_line - 1);

  for (int i = 0; i < NUM_DISPLAY_BUFS; i++) {
    this->display_bufs_[i] = (uint8_t *)heap_caps_aligned_alloc(cache_line, aligned_size, MALLOC_CAP_SPIRAM);
    if (this->display_bufs_[i] == nullptr) {
      ESP_LOGW(TAG, "Failed to alloc display buf %d (%u KB) - async pipeline disabled",
               i, (unsigned)(aligned_size / 1024));
      for (int j = 0; j < i; j++) {
        heap_caps_free(this->display_bufs_[j]);
        this->display_bufs_[j] = nullptr;
      }
      ppa_unregister_client(this->ppa_srm_client_);
      this->ppa_srm_client_ = nullptr;
      return false;
    }
  }
  this->display_buf_w_ = canvas_w;
  this->display_buf_h_ = canvas_h;
  this->display_buf_size_ = aligned_size;

  this->free_queue_ = xQueueCreate(NUM_DISPLAY_BUFS, sizeof(uint8_t *));
  this->filled_queue_ = xQueueCreate(NUM_DISPLAY_BUFS, sizeof(uint8_t *));
  this->ppa_done_sem_ = xSemaphoreCreateBinary();
  if (!this->free_queue_ || !this->filled_queue_ || !this->ppa_done_sem_) {
    ESP_LOGW(TAG, "Failed to create FreeRTOS primitives - async pipeline disabled");
    this->teardown_ppa_async_pipeline_();
    return false;
  }
  for (int i = 0; i < NUM_DISPLAY_BUFS; i++) {
    xQueueSend(this->free_queue_, &this->display_bufs_[i], 0);
  }

  this->producer_should_run_ = true;
  // Pin to core 1 (LVGL/ESPHome main typically run on core 0). Priority 5
  // matches Waveshare's "Camera Detect" task. Stack 4096 covers
  // detection->draw_on_frame() invocations + PPA submit.
  BaseType_t ok = xTaskCreatePinnedToCore(&LVGLCameraDisplay::producer_task_entry_,
                                          "cam_ppa_prod", 4096, this, 5,
                                          &this->producer_task_handle_, 1);
  if (ok != pdPASS) {
    ESP_LOGW(TAG, "Failed to create producer task - async pipeline disabled");
    this->producer_should_run_ = false;
    this->producer_task_handle_ = nullptr;
    this->teardown_ppa_async_pipeline_();
    return false;
  }

  // If the sync path had captured & held a V4L2 buffer in a prior iteration
  // (canvas size not yet known at that time), release it now - the producer
  // task is going to own V4L2 dequeues exclusively from this point.
  if (this->displayed_buffer_ != nullptr) {
    this->camera_->release_buffer(this->displayed_buffer_);
    this->displayed_buffer_ = nullptr;
  }

  this->ppa_async_enabled_ = true;
  this->draw_buf_initialized_ = false;  // lv_draw_buf_t will be (re)init at display size
  ESP_LOGI(TAG, "PPA async pipeline ENABLED: %ux%u (camera) -> %ux%u (canvas)",
           cam_w, cam_h, canvas_w, canvas_h);
  ESP_LOGI(TAG, "   %d display buffers in SPIRAM, %u KB each, %u-byte aligned",
           NUM_DISPLAY_BUFS, (unsigned)(aligned_size / 1024), (unsigned)cache_line);
  return true;
}

void LVGLCameraDisplay::teardown_ppa_async_pipeline_() {
  this->producer_should_run_ = false;
  if (this->producer_task_handle_ != nullptr) {
    // Give the producer time to exit its loop.
    vTaskDelay(pdMS_TO_TICKS(150));
    this->producer_task_handle_ = nullptr;
  }
  if (this->ppa_srm_client_ != nullptr) {
    ppa_unregister_client(this->ppa_srm_client_);
    this->ppa_srm_client_ = nullptr;
  }
  if (this->free_queue_) { vQueueDelete(this->free_queue_); this->free_queue_ = nullptr; }
  if (this->filled_queue_) { vQueueDelete(this->filled_queue_); this->filled_queue_ = nullptr; }
  if (this->ppa_done_sem_) { vSemaphoreDelete(this->ppa_done_sem_); this->ppa_done_sem_ = nullptr; }
  for (int i = 0; i < NUM_DISPLAY_BUFS; i++) {
    if (this->display_bufs_[i] != nullptr) {
      heap_caps_free(this->display_bufs_[i]);
      this->display_bufs_[i] = nullptr;
    }
  }
  this->async_displayed_buf_ = nullptr;
  this->ppa_async_enabled_ = false;
}
#endif  // CONFIG_IDF_TARGET_ESP32P4

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
