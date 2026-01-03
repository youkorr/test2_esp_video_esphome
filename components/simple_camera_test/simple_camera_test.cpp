#include "simple_camera_test.h"

#ifdef USE_ESP_IDF

#include "esphome/core/log.h"
#include "esphome/core/application.h"

extern "C" {
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
}

namespace esphome {
namespace simple_camera_test {

static const char *const TAG = "simple_camera_test";

void SimpleCameraTest::setup() {
  ESP_LOGI(TAG, "=== Simple Camera Test Setup ===");
  ESP_LOGI(TAG, "Direct API calls - bypassing ESPHome layers");
  ESP_LOGI(TAG, "Resolution: %dx%d", this->width_, this->height_);

  // Create mutex for thread-safe buffer access
  this->buffer_mutex_ = xSemaphoreCreateMutex();
  if (!this->buffer_mutex_) {
    ESP_LOGE(TAG, "Failed to create buffer mutex");
    return;
  }

  // Allocate RGB buffer
  this->rgb_buffer_size_ = this->width_ * this->height_ * sizeof(lv_color_t);
  this->rgb_buffer_ = (uint8_t *)heap_caps_malloc(this->rgb_buffer_size_, MALLOC_CAP_SPIRAM);
  if (!this->rgb_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate RGB buffer (%zu bytes)", this->rgb_buffer_size_);
    return;
  }
  ESP_LOGI(TAG, "Allocated RGB buffer: %zu bytes in SPIRAM", this->rgb_buffer_size_);

  // Create LVGL UI
  if (this->parent_) {
    // Create canvas
    this->canvas_ = lv_canvas_create(this->parent_);
    lv_obj_set_size(this->canvas_, this->width_, this->height_);
    lv_obj_align(this->canvas_, LV_ALIGN_CENTER, 0, 0);
    lv_canvas_set_buffer(this->canvas_, this->rgb_buffer_, this->width_, this->height_, LV_IMG_CF_TRUE_COLOR);

    // Create FPS label
    this->fps_label_ = lv_label_create(this->parent_);
    lv_obj_align(this->fps_label_, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_text_color(this->fps_label_, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(this->fps_label_, &lv_font_montserrat_20, 0);
    lv_label_set_text(this->fps_label_, "FPS: 0.0");

    ESP_LOGI(TAG, "LVGL canvas created");
  }

  // Start capture task
  this->running_ = true;
  xTaskCreatePinnedToCore(
      capture_task_,
      "camera_capture",
      8192,
      this,
      5,
      &this->capture_task_handle_,
      1  // Pin to core 1
  );

  ESP_LOGI(TAG, "Setup complete - capture task started");
}

void SimpleCameraTest::loop() {
  // Update canvas with latest frame (thread-safe)
  if (this->canvas_ && xSemaphoreTake(this->buffer_mutex_, 0) == pdTRUE) {
    lv_canvas_set_buffer(this->canvas_, this->rgb_buffer_, this->width_, this->height_, LV_IMG_CF_TRUE_COLOR);
    xSemaphoreGive(this->buffer_mutex_);

    // ★ PERFORMANCE: Use lv_refr_now() instead of lv_obj_invalidate()
    // This is the Waveshare/M5Stack method - refreshes only dirty areas
    lv_refr_now(NULL);
  }

  // Update FPS display every second
  uint32_t now = millis();
  if (now - this->last_fps_time_ >= 1000) {
    calculate_fps_();

    char fps_text[32];
    snprintf(fps_text, sizeof(fps_text), "FPS: %.2f", this->current_fps_);

    if (this->fps_label_) {
      lv_label_set_text(this->fps_label_, fps_text);
    }

    ESP_LOGI(TAG, "★ Direct Camera FPS: %.2f (V4L2 MMAP + lv_refr_now)", this->current_fps_);

    this->frame_count_ = 0;
    this->last_fps_time_ = now;
  }
}

void SimpleCameraTest::dump_config() {
  ESP_LOGCONFIG(TAG, "Simple Camera Test:");
  ESP_LOGCONFIG(TAG, "  Resolution: %dx%d", this->width_, this->height_);
  ESP_LOGCONFIG(TAG, "  Mode: Direct V4L2 API (no ESPHome wrappers)");
  ESP_LOGCONFIG(TAG, "  Render: lv_refr_now() (Waveshare method)");
}

void SimpleCameraTest::capture_task_(void *arg) {
  SimpleCameraTest *self = (SimpleCameraTest *)arg;
  ESP_LOGI(TAG, "=== Capture Task Started ===");

  // Open V4L2 device directly (like M5Stack/Waveshare)
  int video_fd = open("/dev/video0", O_RDWR);
  if (video_fd < 0) {
    ESP_LOGE(TAG, "Failed to open /dev/video0");
    self->running_ = false;
    vTaskDelete(NULL);
    return;
  }
  ESP_LOGI(TAG, "Opened /dev/video0 successfully");

  // Set format
  struct v4l2_format fmt;
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = self->width_;
  fmt.fmt.pix.height = self->height_;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;

  if (ioctl(video_fd, VIDIOC_S_FMT, &fmt) < 0) {
    ESP_LOGE(TAG, "Failed to set format");
    close(video_fd);
    self->running_ = false;
    vTaskDelete(NULL);
    return;
  }
  ESP_LOGI(TAG, "Format set: %dx%d RGB565", fmt.fmt.pix.width, fmt.fmt.pix.height);

  // Request buffers (4 buffers for DMA pipelining)
  struct v4l2_requestbuffers req;
  memset(&req, 0, sizeof(req));
  req.count = 4;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if (ioctl(video_fd, VIDIOC_REQBUFS, &req) < 0) {
    ESP_LOGE(TAG, "Failed to request buffers");
    close(video_fd);
    self->running_ = false;
    vTaskDelete(NULL);
    return;
  }
  ESP_LOGI(TAG, "Allocated %d MMAP buffers", req.count);

  // Map buffers
  struct {
    void *start;
    size_t length;
  } buffers[4];

  for (unsigned int i = 0; i < req.count; i++) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;

    if (ioctl(video_fd, VIDIOC_QUERYBUF, &buf) < 0) {
      ESP_LOGE(TAG, "Failed to query buffer %d", i);
      close(video_fd);
      self->running_ = false;
      vTaskDelete(NULL);
      return;
    }

    buffers[i].length = buf.length;
    buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, video_fd, buf.m.offset);

    if (buffers[i].start == MAP_FAILED) {
      ESP_LOGE(TAG, "Failed to mmap buffer %d", i);
      close(video_fd);
      self->running_ = false;
      vTaskDelete(NULL);
      return;
    }

    // Queue buffer
    if (ioctl(video_fd, VIDIOC_QBUF, &buf) < 0) {
      ESP_LOGE(TAG, "Failed to queue buffer %d", i);
      close(video_fd);
      self->running_ = false;
      vTaskDelete(NULL);
      return;
    }
  }
  ESP_LOGI(TAG, "All buffers mapped and queued");

  // Start streaming
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(video_fd, VIDIOC_STREAMON, &type) < 0) {
    ESP_LOGE(TAG, "Failed to start streaming");
    close(video_fd);
    self->running_ = false;
    vTaskDelete(NULL);
    return;
  }
  ESP_LOGI(TAG, "★ Streaming started - Direct V4L2 capture loop");

  // Capture loop
  while (self->running_) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    // Dequeue buffer (wait for frame)
    if (ioctl(video_fd, VIDIOC_DQBUF, &buf) < 0) {
      ESP_LOGW(TAG, "Failed to dequeue buffer");
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    // Copy frame data to RGB buffer
    if (xSemaphoreTake(self->buffer_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
      memcpy(self->rgb_buffer_, buffers[buf.index].start,
             std::min((size_t)buf.bytesused, self->rgb_buffer_size_));
      xSemaphoreGive(self->buffer_mutex_);

      self->frame_count_++;
    }

    // Queue buffer back
    if (ioctl(video_fd, VIDIOC_QBUF, &buf) < 0) {
      ESP_LOGW(TAG, "Failed to requeue buffer");
    }

    // Update display in main loop (not here to avoid LVGL thread issues)
    // Just increment frame counter
  }

  // Stop streaming
  if (ioctl(video_fd, VIDIOC_STREAMOFF, &type) < 0) {
    ESP_LOGE(TAG, "Failed to stop streaming");
  }

  // Cleanup
  for (unsigned int i = 0; i < req.count; i++) {
    munmap(buffers[i].start, buffers[i].length);
  }
  close(video_fd);

  ESP_LOGI(TAG, "Capture task stopped");
  vTaskDelete(NULL);
}

void SimpleCameraTest::calculate_fps_() {
  uint32_t now = millis();
  uint32_t elapsed = now - this->last_fps_time_;
  if (elapsed > 0) {
    this->current_fps_ = (this->frame_count_ * 1000.0f) / elapsed;
  }
}

}  // namespace simple_camera_test
}  // namespace esphome

#endif
