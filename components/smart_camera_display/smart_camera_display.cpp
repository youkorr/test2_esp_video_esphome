#include "smart_camera_display.h"

#ifdef USE_ESP_IDF

#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace smart_camera_display {

static const char *const TAG = "smart_camera_display";

void SmartCameraDisplay::setup() {
  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "Smart Camera Display Setup");
  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "Camera: %s", this->camera_model_.c_str());
  ESP_LOGI(TAG, "Resolution: %dx%d", this->width_, this->height_);
  ESP_LOGI(TAG, "DMA Optimization: %s", this->enable_dma_opt_ ? "ENABLED" : "DISABLED");
  ESP_LOGI(TAG, "Controls: %s", this->show_controls_ ? "ENABLED" : "DISABLED");

  // Create synchronization
  this->frame_ready_sem_ = xSemaphoreCreateBinary();
  this->swap_mutex_ = xSemaphoreCreateMutex();

  if (!this->frame_ready_sem_ || !this->swap_mutex_) {
    ESP_LOGE(TAG, "Failed to create semaphores");
    return;
  }

  // Allocate buffers
  if (!alloc_buffers_()) {
    ESP_LOGE(TAG, "Failed to allocate buffers");
    return;
  }

  // Initialize camera V4L2
  if (!init_camera_()) {
    ESP_LOGE(TAG, "Failed to initialize camera");
    return;
  }

  // Create LVGL controls if requested
  if (this->show_controls_ && this->parent_) {
    create_controls_();
  }

  // Setup canvas if provided
  if (this->canvas_) {
    lv_canvas_set_buffer(this->canvas_, this->display_buffer_,
                        this->width_, this->height_, LV_IMG_CF_TRUE_COLOR);
    ESP_LOGI(TAG, "Canvas linked to display buffer");
  }

  ESP_LOGI(TAG, "✅ Setup complete");

  // Auto-start if requested
  if (this->auto_start_) {
    start_streaming();
  }
}

void SmartCameraDisplay::loop() {
  // Update FPS counter
  uint32_t now = millis();
  if (now - this->last_fps_time_ >= 1000) {
    uint32_t elapsed = now - this->last_fps_time_;
    if (elapsed > 0) {
      this->current_fps_ = (this->frame_count_ * 1000.0f) / elapsed;
    }

    // Update FPS label if it exists
    if (this->fps_label_) {
      char fps_text[32];
      snprintf(fps_text, sizeof(fps_text), "FPS: %.1f", this->current_fps_);
      lv_label_set_text(this->fps_label_, fps_text);
    }

    if (this->streaming_) {
      ESP_LOGI(TAG, "★ FPS: %.2f (%s)", this->current_fps_,
               this->enable_dma_opt_ ? "DMA optimized" : "Standard");
    }

    this->frame_count_ = 0;
    this->last_fps_time_ = now;
  }

  // Update canvas with latest frame
  if (this->streaming_ && this->canvas_) {
    // Swap buffers (atomic)
    if (xSemaphoreTake(this->swap_mutex_, 0) == pdTRUE) {
      uint8_t *temp = this->swap_buffer_;
      this->swap_buffer_ = this->camera_buffer_;
      this->camera_buffer_ = temp;

      // Copy swap to display
      memcpy(this->display_buffer_, this->swap_buffer_, this->buffer_size_);
      xSemaphoreGive(this->swap_mutex_);

      // Update canvas
      update_canvas_();
    }
  }
}

void SmartCameraDisplay::dump_config() {
  ESP_LOGCONFIG(TAG, "Smart Camera Display:");
  ESP_LOGCONFIG(TAG, "  Camera Model: %s", this->camera_model_.c_str());
  ESP_LOGCONFIG(TAG, "  Resolution: %dx%d", this->width_, this->height_);
  ESP_LOGCONFIG(TAG, "  Position: (%d, %d)", this->x_offset_, this->y_offset_);
  ESP_LOGCONFIG(TAG, "  DMA Optimization: %s", this->enable_dma_opt_ ? "Yes" : "No");
  ESP_LOGCONFIG(TAG, "  Triple Buffering: %s", this->enable_dma_opt_ ? "Yes" : "No");
  ESP_LOGCONFIG(TAG, "  Show Controls: %s", this->show_controls_ ? "Yes" : "No");
  ESP_LOGCONFIG(TAG, "  Auto Start: %s", this->auto_start_ ? "Yes" : "No");
}

bool SmartCameraDisplay::alloc_buffers_() {
  this->buffer_size_ = this->width_ * this->height_ * sizeof(uint16_t);  // RGB565

  ESP_LOGI(TAG, "Allocating buffers...");
  ESP_LOGI(TAG, "  Size: %zu bytes per buffer", this->buffer_size_);

  if (this->enable_dma_opt_) {
    // Triple buffering (PSRAM aligned for DMA)
    ESP_LOGI(TAG, "  Mode: Triple buffering (DMA optimized)");
    ESP_LOGI(TAG, "  Total PSRAM: %zu bytes", this->buffer_size_ * 3);

    this->camera_buffer_ = (uint8_t *)heap_caps_aligned_alloc(
        64, this->buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    this->display_buffer_ = (uint8_t *)heap_caps_aligned_alloc(
        64, this->buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    this->swap_buffer_ = (uint8_t *)heap_caps_aligned_alloc(
        64, this->buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);

    if (!this->camera_buffer_ || !this->display_buffer_ || !this->swap_buffer_) {
      ESP_LOGE(TAG, "Failed to allocate triple buffers");
      free_buffers_();
      return false;
    }

    memset(this->camera_buffer_, 0, this->buffer_size_);
    memset(this->display_buffer_, 0, this->buffer_size_);
    memset(this->swap_buffer_, 0, this->buffer_size_);

    ESP_LOGI(TAG, "✅ Triple buffers allocated (64-byte aligned)");
  } else {
    // Single buffer (standard)
    ESP_LOGI(TAG, "  Mode: Single buffering");

    this->display_buffer_ = (uint8_t *)heap_caps_malloc(
        this->buffer_size_, MALLOC_CAP_SPIRAM);

    if (!this->display_buffer_) {
      ESP_LOGE(TAG, "Failed to allocate display buffer");
      return false;
    }

    memset(this->display_buffer_, 0, this->buffer_size_);
    ESP_LOGI(TAG, "✅ Display buffer allocated");
  }

  return true;
}

void SmartCameraDisplay::free_buffers_() {
  if (this->camera_buffer_) {
    heap_caps_free(this->camera_buffer_);
    this->camera_buffer_ = nullptr;
  }
  if (this->display_buffer_) {
    heap_caps_free(this->display_buffer_);
    this->display_buffer_ = nullptr;
  }
  if (this->swap_buffer_) {
    heap_caps_free(this->swap_buffer_);
    this->swap_buffer_ = nullptr;
  }
}

bool SmartCameraDisplay::init_camera_() {
  ESP_LOGI(TAG, "Opening /dev/video0...");

  this->video_fd_ = open("/dev/video0", O_RDWR);
  if (this->video_fd_ < 0) {
    ESP_LOGE(TAG, "Failed to open /dev/video0");
    return false;
  }

  // Set format
  struct v4l2_format fmt;
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = this->width_;
  fmt.fmt.pix.height = this->height_;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;

  if (ioctl(this->video_fd_, VIDIOC_S_FMT, &fmt) < 0) {
    ESP_LOGE(TAG, "Failed to set format");
    close(this->video_fd_);
    this->video_fd_ = -1;
    return false;
  }

  // Request MMAP buffers
  struct v4l2_requestbuffers req;
  memset(&req, 0, sizeof(req));
  req.count = 4;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if (ioctl(this->video_fd_, VIDIOC_REQBUFS, &req) < 0) {
    ESP_LOGE(TAG, "Failed to request buffers");
    close(this->video_fd_);
    this->video_fd_ = -1;
    return false;
  }

  this->v4l2_buffer_count_ = req.count;
  ESP_LOGI(TAG, "V4L2: %d MMAP buffers allocated", this->v4l2_buffer_count_);

  // Map and queue buffers
  for (unsigned int i = 0; i < this->v4l2_buffer_count_; i++) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;

    if (ioctl(this->video_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
      ESP_LOGE(TAG, "Failed to query buffer %d", i);
      cleanup_camera_();
      return false;
    }

    this->v4l2_buffers_[i].length = buf.length;
    this->v4l2_buffers_[i].start = mmap(NULL, buf.length,
                                        PROT_READ | PROT_WRITE, MAP_SHARED,
                                        this->video_fd_, buf.m.offset);

    if (this->v4l2_buffers_[i].start == MAP_FAILED) {
      ESP_LOGE(TAG, "Failed to mmap buffer %d", i);
      cleanup_camera_();
      return false;
    }

    if (ioctl(this->video_fd_, VIDIOC_QBUF, &buf) < 0) {
      ESP_LOGE(TAG, "Failed to queue buffer %d", i);
      cleanup_camera_();
      return false;
    }
  }

  ESP_LOGI(TAG, "✅ Camera V4L2 initialized");
  return true;
}

void SmartCameraDisplay::cleanup_camera_() {
  if (this->video_fd_ >= 0) {
    for (unsigned int i = 0; i < this->v4l2_buffer_count_; i++) {
      if (this->v4l2_buffers_[i].start != MAP_FAILED) {
        munmap(this->v4l2_buffers_[i].start, this->v4l2_buffers_[i].length);
      }
    }
    close(this->video_fd_);
    this->video_fd_ = -1;
  }
}

void SmartCameraDisplay::create_controls_() {
  if (!this->parent_) {
    ESP_LOGW(TAG, "Cannot create controls: parent not set");
    return;
  }

  ESP_LOGI(TAG, "Creating LVGL controls...");

  // Play button
  this->play_btn_ = lv_btn_create(this->parent_);
  lv_obj_set_size(this->play_btn_, 100, 45);
  lv_obj_set_pos(this->play_btn_, 10, 65);
  lv_obj_set_style_bg_color(this->play_btn_, lv_color_hex(0x00AA00), 0);
  lv_obj_add_event_cb(this->play_btn_, play_btn_cb_, LV_EVENT_CLICKED, this);

  lv_obj_t *play_label = lv_label_create(this->play_btn_);
  lv_label_set_text(play_label, "PLAY");
  lv_obj_center(play_label);

  // Stop button
  this->stop_btn_ = lv_btn_create(this->parent_);
  lv_obj_set_size(this->stop_btn_, 100, 45);
  lv_obj_set_pos(this->stop_btn_, 10, 120);
  lv_obj_set_style_bg_color(this->stop_btn_, lv_color_hex(0xCC0000), 0);
  lv_obj_add_event_cb(this->stop_btn_, stop_btn_cb_, LV_EVENT_CLICKED, this);

  lv_obj_t *stop_label = lv_label_create(this->stop_btn_);
  lv_label_set_text(stop_label, "STOP");
  lv_obj_center(stop_label);

  // Info button
  this->info_btn_ = lv_btn_create(this->parent_);
  lv_obj_set_size(this->info_btn_, 100, 45);
  lv_obj_set_pos(this->info_btn_, 10, 175);
  lv_obj_set_style_bg_color(this->info_btn_, lv_color_hex(0x4682B4), 0);
  lv_obj_add_event_cb(this->info_btn_, info_btn_cb_, LV_EVENT_CLICKED, this);

  lv_obj_t *info_label = lv_label_create(this->info_btn_);
  lv_label_set_text(info_label, "INFO");
  lv_obj_center(info_label);

  // Status label
  this->status_label_ = lv_label_create(this->parent_);
  lv_obj_set_pos(this->status_label_, 120, 70);
  lv_obj_set_style_text_color(this->status_label_, lv_color_hex(0xFFFF00), 0);
  lv_label_set_text(this->status_label_, "READY");

  // FPS label
  this->fps_label_ = lv_label_create(this->parent_);
  lv_obj_set_pos(this->fps_label_, 120, 100);
  lv_obj_set_style_text_color(this->fps_label_, lv_color_hex(0x00FF00), 0);
  lv_label_set_text(this->fps_label_, "FPS: 0.0");

  ESP_LOGI(TAG, "✅ Controls created");
}

void SmartCameraDisplay::update_canvas_() {
  if (!this->canvas_) {
    return;
  }

  lv_canvas_set_buffer(this->canvas_, this->display_buffer_,
                      this->width_, this->height_, LV_IMG_CF_TRUE_COLOR);

  // ★ PERFORMANCE: lv_refr_now() au lieu de lv_obj_invalidate()
  lv_refr_now(NULL);
}

void SmartCameraDisplay::start_streaming() {
  if (this->streaming_) {
    ESP_LOGW(TAG, "Already streaming");
    return;
  }

  ESP_LOGI(TAG, "Starting streaming...");

  // Start V4L2
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(this->video_fd_, VIDIOC_STREAMON, &type) < 0) {
    ESP_LOGE(TAG, "Failed to start V4L2 streaming");
    return;
  }

  this->streaming_ = true;
  this->task_running_ = true;
  this->frame_count_ = 0;
  this->last_fps_time_ = millis();

  // Create capture task
  xTaskCreatePinnedToCore(
      capture_task_,
      "smart_camera",
      8192,
      this,
      10,
      &this->capture_task_handle_,
      1  // Core 1
  );

  if (this->status_label_) {
    lv_label_set_text(this->status_label_, "LIVE");
    lv_obj_set_style_text_color(this->status_label_, lv_color_hex(0x00FF00), 0);
  }

  ESP_LOGI(TAG, "✅ Streaming started");
}

void SmartCameraDisplay::stop_streaming() {
  if (!this->streaming_) {
    return;
  }

  ESP_LOGI(TAG, "Stopping streaming...");

  this->task_running_ = false;
  this->streaming_ = false;

  vTaskDelay(pdMS_TO_TICKS(100));

  if (this->capture_task_handle_) {
    vTaskDelete(this->capture_task_handle_);
    this->capture_task_handle_ = nullptr;
  }

  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  ioctl(this->video_fd_, VIDIOC_STREAMOFF, &type);

  if (this->status_label_) {
    lv_label_set_text(this->status_label_, "STOPPED");
    lv_obj_set_style_text_color(this->status_label_, lv_color_hex(0xFF8800), 0);
  }

  ESP_LOGI(TAG, "Streaming stopped");
}

void SmartCameraDisplay::capture_task_(void *arg) {
  SmartCameraDisplay *self = (SmartCameraDisplay *)arg;
  ESP_LOGI(TAG, "[Capture Task] Started");

  uint8_t *target_buffer = self->enable_dma_opt_ ? self->camera_buffer_ : self->display_buffer_;

  while (self->task_running_) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(self->video_fd_, VIDIOC_DQBUF, &buf) < 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    // Copy to target buffer
    if (xSemaphoreTake(self->swap_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
      memcpy(target_buffer, self->v4l2_buffers_[buf.index].start,
             std::min((size_t)buf.bytesused, self->buffer_size_));
      xSemaphoreGive(self->swap_mutex_);

      self->frame_count_++;
      xSemaphoreGive(self->frame_ready_sem_);
    }

    ioctl(self->video_fd_, VIDIOC_QBUF, &buf);
  }

  ESP_LOGI(TAG, "[Capture Task] Stopped");
  vTaskDelete(NULL);
}

// Button callbacks
void SmartCameraDisplay::play_btn_cb_(lv_event_t *e) {
  SmartCameraDisplay *self = (SmartCameraDisplay *)lv_event_get_user_data(e);
  self->start_streaming();
}

void SmartCameraDisplay::stop_btn_cb_(lv_event_t *e) {
  SmartCameraDisplay *self = (SmartCameraDisplay *)lv_event_get_user_data(e);
  self->stop_streaming();
}

void SmartCameraDisplay::info_btn_cb_(lv_event_t *e) {
  SmartCameraDisplay *self = (SmartCameraDisplay *)lv_event_get_user_data(e);
  ESP_LOGI(TAG, "=== CAMERA INFO ===");
  ESP_LOGI(TAG, "Model: %s", self->camera_model_.c_str());
  ESP_LOGI(TAG, "Resolution: %dx%d", self->width_, self->height_);
  ESP_LOGI(TAG, "FPS: %.2f", self->current_fps_);
  ESP_LOGI(TAG, "DMA Opt: %s", self->enable_dma_opt_ ? "Yes" : "No");
  ESP_LOGI(TAG, "Streaming: %s", self->streaming_ ? "Yes" : "No");
}

}  // namespace smart_camera_display
}  // namespace esphome

#endif  // USE_ESP_IDF
