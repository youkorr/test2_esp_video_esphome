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
  ESP_LOGI(TAG, "  MIPI CSI Camera Setup (V4L2 + PPA)");
  ESP_LOGI(TAG, "========================================");

  ESP_LOGI(TAG, "Configuration:");
  ESP_LOGI(TAG, "  Sensor: %s", this->sensor_.c_str());
  ESP_LOGI(TAG, "  External Clock: GPIO%d @ %u Hz", this->external_clock_pin_, this->frequency_);
  ESP_LOGI(TAG, "  Résolution: %s", this->resolution_.c_str());
  ESP_LOGI(TAG, "  Format: %s", this->pixel_format_.c_str());
  ESP_LOGI(TAG, "  FPS: %d", this->framerate_);
  ESP_LOGI(TAG, "  JPEG Quality: %d", this->jpeg_quality_);
  ESP_LOGI(TAG, "  Mirror X: %s", this->mirror_x_ ? "Oui" : "Non");
  ESP_LOGI(TAG, "  Mirror Y: %s", this->mirror_y_ ? "Oui" : "Non");
  ESP_LOGI(TAG, "  Rotation: %d°", this->rotation_angle_);

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

  // Setup PPA (Pixel Processing Accelerator)
  if (!this->setup_ppa_()) {
    ESP_LOGE(TAG, "❌ Échec configuration PPA");
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

bool MipiDSICamComponent::setup_ppa_() {
  ESP_LOGI(TAG, "Configuration PPA...");

  // Allouer buffer de sortie dans SPIRAM avec capacité DMA
  this->output_buffer_size_ = this->frame_size_;
  this->output_buffer_ = (uint8_t*)heap_caps_calloc(
    this->output_buffer_size_, 1,
    MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM
  );

  if (this->output_buffer_ == nullptr) {
    ESP_LOGE(TAG, "❌ Échec allocation buffer sortie (%u octets)", (unsigned)this->output_buffer_size_);
    return false;
  }

  ESP_LOGI(TAG, "  Buffer sortie: %u octets (DMA+SPIRAM)", (unsigned)this->output_buffer_size_);

  // Configuration PPA
  ppa_client_config_t ppa_config = {
    .oper_type = PPA_OPERATION_SRM,  // Scale, Rotate, Mirror
    .max_pending_trans_num = 1,
  };

  esp_err_t ret = ppa_register_client(&ppa_config, &this->ppa_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "❌ ppa_register_client failed: %d", ret);
    heap_caps_free(this->output_buffer_);
    this->output_buffer_ = nullptr;
    return false;
  }

  ESP_LOGI(TAG, "✓ PPA configuré (SRM mode)");
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

  if (!this->streaming_ || this->video_fd_ < 0 || this->ppa_handle_ == nullptr) {
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

  // Traiter l'image avec le PPA (Scale, Rotate, Mirror)
  ppa_srm_oper_config_t srm_config = {
    .in = {
      .buffer = this->buffers_[buf.index],
      .pic_w = this->width_,
      .pic_h = this->height_,
      .block_w = this->width_,
      .block_h = this->height_,
      .block_offset_x = 0,
      .block_offset_y = 0,
      .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
    },
    .out = {
      .buffer = this->output_buffer_,
      .buffer_size = this->output_buffer_size_,
      .pic_w = this->width_,
      .pic_h = this->height_,
      .block_offset_x = 0,
      .block_offset_y = 0,
      .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
    },
    .rotation_angle = this->map_rotation_(this->rotation_angle_),
    .scale_x = 1.0f,
    .scale_y = 1.0f,
    .mirror_x = this->mirror_x_,
    .mirror_y = this->mirror_y_,
    .rgb_swap = false,
    .byte_swap = false,
    .mode = PPA_TRANS_MODE_BLOCKING,  // Bloquant pour éviter des problèmes de synchronisation
  };

  esp_err_t ret = ppa_do_scale_rotate_mirror(this->ppa_handle_, &srm_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "ppa_do_scale_rotate_mirror failed: %d", ret);
  }

  // Requeue buffer (rend le buffer au driver)
  if (ioctl(this->video_fd_, VIDIOC_QBUF, &buf) < 0) {
    ESP_LOGE(TAG, "VIDIOC_QBUF failed");
    return false;
  }

  return (ret == ESP_OK);
}

uint8_t* MipiDSICamComponent::get_image_data() {
  std::lock_guard<std::mutex> lock(this->camera_mutex_);
  // Retourne le buffer de sortie PPA (avec mirror/rotation appliquée)
  return this->output_buffer_;
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

ppa_srm_rotation_angle_t MipiDSICamComponent::map_rotation_(int angle) {
  switch (angle) {
    case 0:   return PPA_SRM_ROTATION_ANGLE_0;
    case 90:  return PPA_SRM_ROTATION_ANGLE_90;
    case 180: return PPA_SRM_ROTATION_ANGLE_180;
    case 270: return PPA_SRM_ROTATION_ANGLE_270;
    default:
      ESP_LOGW(TAG, "Angle de rotation invalide: %d, utilisation 0°", angle);
      return PPA_SRM_ROTATION_ANGLE_0;
  }
}

void MipiDSICamComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "MIPI CSI Camera:");
  ESP_LOGCONFIG(TAG, "  Sensor: %s", this->sensor_.c_str());
  ESP_LOGCONFIG(TAG, "  External Clock: GPIO%d @ %u Hz", this->external_clock_pin_, this->frequency_);
  ESP_LOGCONFIG(TAG, "  Résolution: %s (%dx%d)",
                this->resolution_.c_str(), this->width_, this->height_);
  ESP_LOGCONFIG(TAG, "  Format: %s", this->pixel_format_.c_str());
  ESP_LOGCONFIG(TAG, "  FPS: %d", this->framerate_);
  ESP_LOGCONFIG(TAG, "  JPEG Quality: %d", this->jpeg_quality_);
  ESP_LOGCONFIG(TAG, "  Mirror X: %s", this->mirror_x_ ? "Oui" : "Non");
  ESP_LOGCONFIG(TAG, "  Mirror Y: %s", this->mirror_y_ ? "Oui" : "Non");
  ESP_LOGCONFIG(TAG, "  Rotation: %d°", this->rotation_angle_);
  ESP_LOGCONFIG(TAG, "  État: %s", this->streaming_ ? "Streaming" : "Arrêté");
  ESP_LOGCONFIG(TAG, "  Device: %s", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
  ESP_LOGCONFIG(TAG, "  PPA: %s", this->ppa_handle_ ? "Activé" : "Désactivé");
}

}  // namespace mipi_dsi_cam
}  // namespace esphome
