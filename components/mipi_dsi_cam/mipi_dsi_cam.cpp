#include "mipi_dsi_cam.h"
#include "esphome/core/log.h"

extern "C" {
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "esp_video_device.h"
}

namespace esphome {
namespace mipi_dsi_cam {

static const char *const TAG = "mipi_dsi_cam";

void MipiDSICamComponent::setup() {
  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "  MIPI CSI Camera Setup (V4L2 API)");
  ESP_LOGI(TAG, "========================================");

  ESP_LOGI(TAG, "Configuration:");
  ESP_LOGI(TAG, "  Résolution: %s", this->resolution_.c_str());
  ESP_LOGI(TAG, "  Format: %s", this->pixel_format_.c_str());
  ESP_LOGI(TAG, "  FPS: %d", this->framerate_);

  // Parse resolution
  if (!this->parse_resolution_(this->resolution_, this->width_, this->height_)) {
    ESP_LOGE(TAG, "❌ Résolution invalide: %s", this->resolution_.c_str());
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "  -> %dx%d", this->width_, this->height_);

  // Map pixel format
  this->v4l2_pixelformat_ = this->map_pixel_format_(this->pixel_format_);
  if (this->v4l2_pixelformat_ == 0) {
    ESP_LOGE(TAG, "❌ Format pixel invalide: %s", this->pixel_format_.c_str());
    this->mark_failed();
    return;
  }

  // Calculate frame size
  if (this->v4l2_pixelformat_ == V4L2_PIX_FMT_RGB565) {
    this->frame_size_ = this->width_ * this->height_ * 2;
  } else if (this->v4l2_pixelformat_ == V4L2_PIX_FMT_YUV422P) {
    this->frame_size_ = this->width_ * this->height_ * 2;
  } else {
    this->frame_size_ = this->width_ * this->height_;
  }

  ESP_LOGI(TAG, "  Taille frame: %u octets", (unsigned)this->frame_size_);

  // Open video device
  if (!this->open_video_device_()) {
    ESP_LOGE(TAG, "❌ Échec ouverture device vidéo");
    this->mark_failed();
    return;
  }

  // Setup buffers
  if (!this->setup_buffers_()) {
    ESP_LOGE(TAG, "❌ Échec configuration buffers");
    this->mark_failed();
    return;
  }

  // Start streaming
  if (!this->start_stream_()) {
    ESP_LOGE(TAG, "❌ Échec démarrage streaming");
    this->mark_failed();
    return;
  }

  this->initialized_ = true;
  this->streaming_ = true;

  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "✅ Caméra prête");
  ESP_LOGI(TAG, "========================================");
}

bool MipiDSICamComponent::open_video_device_() {
  ESP_LOGI(TAG, "Ouverture %s...", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);

  this->video_fd_ = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
  if (this->video_fd_ < 0) {
    ESP_LOGE(TAG, "Échec open(): errno=%d (%s)", errno, strerror(errno));
    return false;
  }

  // Query capabilities
  struct v4l2_capability cap;
  if (ioctl(this->video_fd_, VIDIOC_QUERYCAP, &cap) < 0) {
    ESP_LOGE(TAG, "VIDIOC_QUERYCAP failed");
    close(this->video_fd_);
    this->video_fd_ = -1;
    return false;
  }

  ESP_LOGI(TAG, "Device info:");
  ESP_LOGI(TAG, "  Driver: %s", cap.driver);
  ESP_LOGI(TAG, "  Card: %s", cap.card);
  ESP_LOGI(TAG, "  Version: %d.%d.%d",
           (cap.version >> 16) & 0xFF,
           (cap.version >> 8) & 0xFF,
           cap.version & 0xFF);

  // Get current format
  struct v4l2_format fmt;
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (ioctl(this->video_fd_, VIDIOC_G_FMT, &fmt) < 0) {
    ESP_LOGE(TAG, "VIDIOC_G_FMT failed");
    close(this->video_fd_);
    this->video_fd_ = -1;
    return false;
  }

  ESP_LOGI(TAG, "Format actuel: %dx%d, fourcc=0x%08X",
           fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.pixelformat);

  // Set desired format
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = this->width_;
  fmt.fmt.pix.height = this->height_;
  fmt.fmt.pix.pixelformat = this->v4l2_pixelformat_;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;

  if (ioctl(this->video_fd_, VIDIOC_S_FMT, &fmt) < 0) {
    ESP_LOGE(TAG, "VIDIOC_S_FMT failed");
    close(this->video_fd_);
    this->video_fd_ = -1;
    return false;
  }

  ESP_LOGI(TAG, "Format configuré: %dx%d, fourcc=0x%08X",
           fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.pixelformat);

  // Set framerate
  struct v4l2_streamparm parm;
  memset(&parm, 0, sizeof(parm));
  parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  parm.parm.capture.timeperframe.numerator = 1;
  parm.parm.capture.timeperframe.denominator = this->framerate_;

  if (ioctl(this->video_fd_, VIDIOC_S_PARM, &parm) < 0) {
    ESP_LOGW(TAG, "VIDIOC_S_PARM failed, framerate non configuré");
  } else {
    ESP_LOGI(TAG, "Framerate configuré: %d FPS", this->framerate_);
  }

  return true;
}

bool MipiDSICamComponent::setup_buffers_() {
  ESP_LOGI(TAG, "Configuration buffers...");

  // Request buffers
  struct v4l2_requestbuffers req;
  memset(&req, 0, sizeof(req));
  req.count = VIDEO_BUFFER_COUNT;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if (ioctl(this->video_fd_, VIDIOC_REQBUFS, &req) < 0) {
    ESP_LOGE(TAG, "VIDIOC_REQBUFS failed");
    return false;
  }

  ESP_LOGI(TAG, "  Buffers alloués: %d", req.count);

  // Map buffers
  for (int i = 0; i < VIDEO_BUFFER_COUNT; i++) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;

    if (ioctl(this->video_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
      ESP_LOGE(TAG, "VIDIOC_QUERYBUF failed for buffer %d", i);
      return false;
    }

    this->buffers_[i] = (uint8_t*)mmap(
      nullptr,
      buf.length,
      PROT_READ | PROT_WRITE,
      MAP_SHARED,
      this->video_fd_,
      buf.m.offset
    );

    if (this->buffers_[i] == MAP_FAILED) {
      ESP_LOGE(TAG, "mmap failed for buffer %d", i);
      return false;
    }

    ESP_LOGI(TAG, "  Buffer %d: mmap OK (%u octets)", i, buf.length);

    // Queue buffer
    if (ioctl(this->video_fd_, VIDIOC_QBUF, &buf) < 0) {
      ESP_LOGE(TAG, "VIDIOC_QBUF failed for buffer %d", i);
      return false;
    }
  }

  return true;
}

bool MipiDSICamComponent::start_stream_() {
  ESP_LOGI(TAG, "Démarrage streaming...");

  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(this->video_fd_, VIDIOC_STREAMON, &type) < 0) {
    ESP_LOGE(TAG, "VIDIOC_STREAMON failed");
    return false;
  }

  ESP_LOGI(TAG, "✓ Streaming démarré");
  return true;
}

bool MipiDSICamComponent::stop_stream_() {
  if (this->video_fd_ < 0) {
    return false;
  }

  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(this->video_fd_, VIDIOC_STREAMOFF, &type) < 0) {
    ESP_LOGE(TAG, "VIDIOC_STREAMOFF failed");
    return false;
  }

  ESP_LOGI(TAG, "Streaming arrêté");
  return true;
}

void MipiDSICamComponent::loop() {
  // Rien à faire - le streaming est géré par capture_frame() appelé depuis lvgl_camera_display
}

bool MipiDSICamComponent::start_streaming() {
  std::lock_guard<std::mutex> lock(this->camera_mutex_);

  if (this->streaming_) {
    return true;
  }

  if (!this->start_stream_()) {
    return false;
  }

  this->streaming_ = true;
  return true;
}

bool MipiDSICamComponent::stop_streaming() {
  std::lock_guard<std::mutex> lock(this->camera_mutex_);

  if (!this->streaming_) {
    return true;
  }

  if (!this->stop_stream_()) {
    return false;
  }

  this->streaming_ = false;
  return true;
}

bool MipiDSICamComponent::is_streaming() const {
  return this->streaming_;
}

bool MipiDSICamComponent::capture_frame() {
  std::lock_guard<std::mutex> lock(this->camera_mutex_);

  if (!this->streaming_ || this->video_fd_ < 0) {
    return false;
  }

  // Dequeue buffer (récupère une frame)
  struct v4l2_buffer buf;
  memset(&buf, 0, sizeof(buf));
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;

  if (ioctl(this->video_fd_, VIDIOC_DQBUF, &buf) < 0) {
    if (errno != EAGAIN) {
      ESP_LOGE(TAG, "VIDIOC_DQBUF failed: errno=%d (%s)", errno, strerror(errno));
    }
    return false;
  }

  // Pointer vers les données de la frame
  this->current_frame_ = this->buffers_[buf.index];

  // Requeue buffer (rend le buffer au driver)
  if (ioctl(this->video_fd_, VIDIOC_QBUF, &buf) < 0) {
    ESP_LOGE(TAG, "VIDIOC_QBUF failed");
    return false;
  }

  return true;
}

uint8_t* MipiDSICamComponent::get_image_data() {
  std::lock_guard<std::mutex> lock(this->camera_mutex_);
  return this->current_frame_;
}

uint32_t MipiDSICamComponent::map_pixel_format_(const std::string &fmt) {
  std::string fmt_upper = fmt;
  std::transform(fmt_upper.begin(), fmt_upper.end(), fmt_upper.begin(), ::toupper);

  if (fmt_upper == "RGB565") return V4L2_PIX_FMT_RGB565;
  if (fmt_upper == "YUV422" || fmt_upper == "YUYV") return V4L2_PIX_FMT_YUV422P;
  if (fmt_upper == "RAW8") return V4L2_PIX_FMT_SBGGR8;

  ESP_LOGW(TAG, "Format inconnu '%s', utilisation RGB565", fmt.c_str());
  return V4L2_PIX_FMT_RGB565;
}

bool MipiDSICamComponent::parse_resolution_(const std::string &res, uint16_t &w, uint16_t &h) {
  std::string res_upper = res;
  std::transform(res_upper.begin(), res_upper.end(), res_upper.begin(), ::toupper);

  if (res_upper == "720P") { w = 1280; h = 720; return true; }
  if (res_upper == "VGA") { w = 640; h = 480; return true; }
  if (res_upper == "QVGA") { w = 320; h = 240; return true; }

  // Parse format "WIDTHxHEIGHT"
  size_t pos = res.find('x');
  if (pos != std::string::npos) {
    try {
      w = std::stoi(res.substr(0, pos));
      h = std::stoi(res.substr(pos + 1));
      return true;
    } catch (...) {
      return false;
    }
  }

  return false;
}

void MipiDSICamComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "MIPI CSI Camera:");
  ESP_LOGCONFIG(TAG, "  Résolution: %s (%dx%d)",
                this->resolution_.c_str(), this->width_, this->height_);
  ESP_LOGCONFIG(TAG, "  Format: %s", this->pixel_format_.c_str());
  ESP_LOGCONFIG(TAG, "  FPS: %d", this->framerate_);
  ESP_LOGCONFIG(TAG, "  État: %s", this->streaming_ ? "Streaming" : "Arrêté");
  ESP_LOGCONFIG(TAG, "  Device: %s", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
}

}  // namespace mipi_dsi_cam
}  // namespace esphome
