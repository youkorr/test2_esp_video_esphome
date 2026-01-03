#include "dma_camera_display.h"

#ifdef USE_ESP_IDF

#include "esphome/core/log.h"
#include "esphome/core/application.h"

// LVGL v8 includes pour récupérer LCD panel
extern "C" {
#include "lvgl.h"
}

namespace esphome {
namespace dma_camera_display {

static const char *const TAG = "dma_camera_display";

void DmaCameraDisplay::setup() {
  ESP_LOGI(TAG, "===========================================");
  ESP_LOGI(TAG, "DMA Camera Display - Zero LVGL Redraw");
  ESP_LOGI(TAG, "Architecture: esp_brookesia");
  ESP_LOGI(TAG, "===========================================");
  ESP_LOGI(TAG, "Resolution: %dx%d", this->width_, this->height_);
  ESP_LOGI(TAG, "Position: (%d, %d)", this->x_offset_, this->y_offset_);
  ESP_LOGI(TAG, "VSync: %s", this->enable_vsync_ ? "ENABLED" : "DISABLED");
  ESP_LOGI(TAG, "Target FPS: %.1f", this->fps_target_);

  // Create synchronization primitives
  this->frame_ready_sem_ = xSemaphoreCreateBinary();
  this->swap_mutex_ = xSemaphoreCreateMutex();

  if (!this->frame_ready_sem_ || !this->swap_mutex_) {
    ESP_LOGE(TAG, "Failed to create synchronization primitives");
    return;
  }

  // Initialize LCD panel (from LVGL or create new)
  if (!init_lcd_panel_()) {
    ESP_LOGE(TAG, "Failed to initialize LCD panel");
    return;
  }

  // Allocate triple buffers in PSRAM (aligned 64 bytes)
  if (!alloc_triple_buffers_()) {
    ESP_LOGE(TAG, "Failed to allocate triple buffers");
    return;
  }

  // Initialize camera V4L2
  if (!init_camera_v4l2_()) {
    ESP_LOGE(TAG, "Failed to initialize camera V4L2");
    return;
  }

  ESP_LOGI(TAG, "✅ Setup complete - DMA pipeline ready");
  ESP_LOGI(TAG, "Call start_display() to begin");
}

void DmaCameraDisplay::loop() {
  // Calculate FPS every second
  uint32_t now = millis();
  if (now - this->last_fps_time_ >= 1000) {
    uint32_t elapsed = now - this->last_fps_time_;
    if (elapsed > 0) {
      this->current_fps_ = (this->frame_count_ * 1000.0f) / elapsed;
    }

    ESP_LOGI(TAG, "★ DMA FPS: %.2f (Camera DMA → LCD DMA, zero LVGL)", this->current_fps_);

    this->frame_count_ = 0;
    this->last_fps_time_ = now;
  }
}

void DmaCameraDisplay::dump_config() {
  ESP_LOGCONFIG(TAG, "DMA Camera Display:");
  ESP_LOGCONFIG(TAG, "  Mode: Zero LVGL (esp_brookesia architecture)");
  ESP_LOGCONFIG(TAG, "  Pipeline: Camera DMA → Buffer → LCD DMA");
  ESP_LOGCONFIG(TAG, "  Buffering: Triple (PSRAM aligned 64 bytes)");
  ESP_LOGCONFIG(TAG, "  Resolution: %dx%d", this->width_, this->height_);
  ESP_LOGCONFIG(TAG, "  VSync: %s", this->enable_vsync_ ? "Yes" : "No");
}

bool DmaCameraDisplay::init_lcd_panel_() {
  ESP_LOGI(TAG, "Retrieving LCD panel handle from LVGL...");

  // Try to get LCD panel from LVGL display driver
  this->lcd_panel_ = get_lcd_panel_from_lvgl_();

  if (this->lcd_panel_) {
    ESP_LOGI(TAG, "✅ Retrieved LCD panel from LVGL");
    this->lcd_panel_owned_ = false;
    return true;
  }

  ESP_LOGW(TAG, "Could not retrieve LCD panel from LVGL");
  ESP_LOGW(TAG, "DMA display will not work without LCD panel handle");
  ESP_LOGW(TAG, "Please ensure LVGL display is initialized first");
  return false;
}

esp_lcd_panel_handle_t DmaCameraDisplay::get_lcd_panel_from_lvgl_() {
  // Get default LVGL display
  lv_disp_t *disp = lv_disp_get_default();
  if (!disp) {
    ESP_LOGW(TAG, "No default LVGL display found");
    return nullptr;
  }

  // Access display driver
  lv_disp_drv_t *driver = &disp->driver;
  if (!driver) {
    ESP_LOGW(TAG, "No LVGL display driver");
    return nullptr;
  }

  // Check if user_data contains LCD panel handle
  // This is the standard pattern for ESP-IDF LVGL integration
  void *user_data = driver->user_data;
  if (!user_data) {
    ESP_LOGW(TAG, "Display driver user_data is NULL");
    ESP_LOGW(TAG, "LCD panel handle not stored in standard location");
    return nullptr;
  }

  // Cast to esp_lcd_panel_handle_t
  // IMPORTANT: This assumes ESPHome LVGL stores panel handle in user_data
  esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)user_data;

  ESP_LOGI(TAG, "LCD panel handle retrieved: %p", panel);
  return panel;
}

bool DmaCameraDisplay::alloc_triple_buffers_() {
  this->buffer_size_ = this->width_ * this->height_ * sizeof(uint16_t);  // RGB565

  ESP_LOGI(TAG, "Allocating triple buffers...");
  ESP_LOGI(TAG, "  Buffer size: %zu bytes each", this->buffer_size_);
  ESP_LOGI(TAG, "  Total PSRAM: %zu bytes", this->buffer_size_ * 3);

  // Allocate aligned buffers in PSRAM (64-byte alignment for DMA)
  this->camera_buffer_ = (uint8_t *)heap_caps_aligned_alloc(
      64, this->buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
  this->display_buffer_ = (uint8_t *)heap_caps_aligned_alloc(
      64, this->buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
  this->swap_buffer_ = (uint8_t *)heap_caps_aligned_alloc(
      64, this->buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);

  if (!this->camera_buffer_ || !this->display_buffer_ || !this->swap_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate triple buffers in PSRAM");
    free_triple_buffers_();
    return false;
  }

  // Clear buffers
  memset(this->camera_buffer_, 0, this->buffer_size_);
  memset(this->display_buffer_, 0, this->buffer_size_);
  memset(this->swap_buffer_, 0, this->buffer_size_);

  ESP_LOGI(TAG, "✅ Triple buffers allocated (PSRAM, 64-byte aligned)");
  return true;
}

void DmaCameraDisplay::free_triple_buffers_() {
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

bool DmaCameraDisplay::init_camera_v4l2_() {
  ESP_LOGI(TAG, "Initializing V4L2 camera...");

  // Open /dev/video0
  this->video_fd_ = open("/dev/video0", O_RDWR);
  if (this->video_fd_ < 0) {
    ESP_LOGE(TAG, "Failed to open /dev/video0");
    return false;
  }

  // Set format (RGB565)
  struct v4l2_format fmt;
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = this->width_;
  fmt.fmt.pix.height = this->height_;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;

  if (ioctl(this->video_fd_, VIDIOC_S_FMT, &fmt) < 0) {
    ESP_LOGE(TAG, "Failed to set V4L2 format");
    close(this->video_fd_);
    this->video_fd_ = -1;
    return false;
  }

  // Request MMAP buffers (4 for DMA pipelining)
  struct v4l2_requestbuffers req;
  memset(&req, 0, sizeof(req));
  req.count = 4;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if (ioctl(this->video_fd_, VIDIOC_REQBUFS, &req) < 0) {
    ESP_LOGE(TAG, "Failed to request V4L2 buffers");
    close(this->video_fd_);
    this->video_fd_ = -1;
    return false;
  }

  this->v4l2_buffer_count_ = req.count;
  ESP_LOGI(TAG, "Allocated %d V4L2 MMAP buffers", this->v4l2_buffer_count_);

  // Map buffers
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

    // Queue buffer
    if (ioctl(this->video_fd_, VIDIOC_QBUF, &buf) < 0) {
      ESP_LOGE(TAG, "Failed to queue buffer %d", i);
      cleanup_camera_();
      return false;
    }
  }

  ESP_LOGI(TAG, "✅ V4L2 camera initialized (MMAP mode)");
  return true;
}

void DmaCameraDisplay::cleanup_camera_() {
  if (this->video_fd_ >= 0) {
    // Unmap buffers
    for (unsigned int i = 0; i < this->v4l2_buffer_count_; i++) {
      if (this->v4l2_buffers_[i].start != MAP_FAILED) {
        munmap(this->v4l2_buffers_[i].start, this->v4l2_buffers_[i].length);
      }
    }

    close(this->video_fd_);
    this->video_fd_ = -1;
  }
}

void DmaCameraDisplay::start_display() {
  if (this->displaying_) {
    ESP_LOGW(TAG, "Display already running");
    return;
  }

  if (!this->lcd_panel_) {
    ESP_LOGE(TAG, "Cannot start: LCD panel not initialized");
    return;
  }

  ESP_LOGI(TAG, "Starting DMA display pipeline...");

  // Start V4L2 streaming
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(this->video_fd_, VIDIOC_STREAMON, &type) < 0) {
    ESP_LOGE(TAG, "Failed to start V4L2 streaming");
    return;
  }

  this->displaying_ = true;
  this->tasks_running_ = true;

  // Create camera capture task (Core 1)
  xTaskCreatePinnedToCore(
      camera_task_,
      "dma_camera",
      8192,
      this,
      10,  // High priority for camera capture
      &this->camera_task_handle_,
      1
  );

  // Create LCD display task (Core 0)
  xTaskCreatePinnedToCore(
      display_task_,
      "dma_display",
      8192,
      this,
      9,  // Slightly lower than camera
      &this->display_task_handle_,
      0
  );

  ESP_LOGI(TAG, "✅ DMA display started");
  ESP_LOGI(TAG, "Camera DMA → Triple Buffer → LCD DMA");
}

void DmaCameraDisplay::stop_display() {
  if (!this->displaying_) {
    return;
  }

  ESP_LOGI(TAG, "Stopping DMA display...");

  this->tasks_running_ = false;
  this->displaying_ = false;

  // Wait for tasks to exit
  vTaskDelay(pdMS_TO_TICKS(100));

  // Delete tasks
  if (this->camera_task_handle_) {
    vTaskDelete(this->camera_task_handle_);
    this->camera_task_handle_ = nullptr;
  }
  if (this->display_task_handle_) {
    vTaskDelete(this->display_task_handle_);
    this->display_task_handle_ = nullptr;
  }

  // Stop V4L2 streaming
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  ioctl(this->video_fd_, VIDIOC_STREAMOFF, &type);

  ESP_LOGI(TAG, "DMA display stopped");
}

void DmaCameraDisplay::camera_task_(void *arg) {
  DmaCameraDisplay *self = (DmaCameraDisplay *)arg;
  ESP_LOGI(TAG, "[Camera Task] Started - capturing frames via DMA");

  while (self->tasks_running_) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    // Dequeue frame (blocking)
    if (ioctl(self->video_fd_, VIDIOC_DQBUF, &buf) < 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    // Copy to camera buffer (DMA source)
    memcpy(self->camera_buffer_, self->v4l2_buffers_[buf.index].start,
           std::min((size_t)buf.bytesused, self->buffer_size_));

    // Requeue buffer
    ioctl(self->video_fd_, VIDIOC_QBUF, &buf);

    // Signal frame ready
    xSemaphoreGive(self->frame_ready_sem_);
  }

  ESP_LOGI(TAG, "[Camera Task] Stopped");
  vTaskDelete(NULL);
}

void DmaCameraDisplay::display_task_(void *arg) {
  DmaCameraDisplay *self = (DmaCameraDisplay *)arg;
  ESP_LOGI(TAG, "[Display Task] Started - sending frames to LCD DMA");

  while (self->tasks_running_) {
    // Wait for camera frame
    if (xSemaphoreTake(self->frame_ready_sem_, pdMS_TO_TICKS(100)) != pdTRUE) {
      continue;
    }

    // ✅ ATOMIC SWAP (esp_brookesia architecture)
    if (xSemaphoreTake(self->swap_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
      uint8_t *temp = self->swap_buffer_;
      self->swap_buffer_ = self->camera_buffer_;
      self->camera_buffer_ = temp;
      xSemaphoreGive(self->swap_mutex_);
    }

    // Wait for VSync (optional, reduces tearing)
    if (self->enable_vsync_) {
      self->wait_vsync_();
    }

    // ✅ DMA DIRECT TO LCD (zero LVGL)
    // This is the KEY: esp_lcd_panel_draw_bitmap uses DMA, no CPU copy
    esp_err_t ret = esp_lcd_panel_draw_bitmap(
        self->lcd_panel_,
        self->x_offset_,
        self->y_offset_,
        self->x_offset_ + self->width_,
        self->y_offset_ + self->height_,
        self->swap_buffer_
    );

    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "LCD DMA failed: %d", ret);
    }

    self->frame_count_++;
  }

  ESP_LOGI(TAG, "[Display Task] Stopped");
  vTaskDelete(NULL);
}

void DmaCameraDisplay::wait_vsync_() {
  // TODO: Implement hardware VSync wait
  // For now, just a small delay to approximate frame timing
  // Real implementation would use:
  // - esp_lcd_panel_wait_for_vsync() if available
  // - GPIO interrupt from LCD VSync pin
  // - MIPI DSI VSync event

  vTaskDelay(pdMS_TO_TICKS(1));  // ~1ms approximation
}

}  // namespace dma_camera_display
}  // namespace esphome

#endif  // USE_ESP_IDF
