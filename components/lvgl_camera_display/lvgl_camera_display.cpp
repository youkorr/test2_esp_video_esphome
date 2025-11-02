#include "lvgl_camera_display.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace lvgl_camera_display {

static const char *const TAG = "lvgl_camera_display";

void LVGLCameraDisplay::setup() {
  ESP_LOGCONFIG(TAG, "🎥 LVGL Camera Display (V4L2 Mode)");

#ifdef USE_ESP32_VARIANT_ESP32P4
  // Récupérer la résolution depuis la caméra
  if (this->camera_) {
    this->width_ = this->camera_->get_image_width();
    this->height_ = this->camera_->get_image_height();
    ESP_LOGI(TAG, "📐 Using camera resolution: %ux%u", this->width_, this->height_);
  } else {
    ESP_LOGW(TAG, "⚠️  No camera linked");
    this->mark_failed();
    return;
  }

  // S'assurer que la caméra est initialisée et en streaming
  if (!this->camera_->is_streaming()) {
    ESP_LOGI(TAG, "Starting camera streaming...");
    if (!this->camera_->start_streaming()) {
      ESP_LOGE(TAG, "❌ Failed to start camera streaming");
      this->mark_failed();
      return;
    }
  }

  // Activer l'adaptateur V4L2 pour avoir /dev/video0
  if (!this->camera_->get_v4l2_adapter()) {
    ESP_LOGI(TAG, "Enabling V4L2 adapter...");
    this->camera_->enable_v4l2_adapter();
    delay(100);
  }

  // Attendre stabilisation
  delay(200);

  // Ouvrir le device V4L2 (comme M5Stack)
  if (!this->open_v4l2_device_()) {
    ESP_LOGE(TAG, "❌ Failed to open V4L2 device");
    this->mark_failed();
    return;
  }

  // Configurer le format V4L2
  if (!this->setup_v4l2_format_()) {
    ESP_LOGE(TAG, "❌ Failed to setup V4L2 format");
    this->mark_failed();
    return;
  }

  // Configurer les buffers V4L2 avec mmap
  if (!this->setup_v4l2_buffers_()) {
    ESP_LOGE(TAG, "❌ Failed to setup V4L2 buffers");
    this->mark_failed();
    return;
  }

  // Démarrer le streaming V4L2
  if (!this->start_v4l2_streaming_()) {
    ESP_LOGE(TAG, "❌ Failed to start V4L2 streaming");
    this->mark_failed();
    return;
  }

  // Initialiser PPA si transformations nécessaires
  if (this->rotation_ != ROTATION_0 || this->mirror_x_ || this->mirror_y_) {
    if (!this->init_ppa_()) {
      ESP_LOGE(TAG, "❌ Failed to initialize PPA");
      this->mark_failed();
      return;
    }
    ESP_LOGI(TAG, "✅ PPA initialized (rotation=%d°, mirror_x=%s, mirror_y=%s)",
             this->rotation_, this->mirror_x_ ? "ON" : "OFF", 
             this->mirror_y_ ? "ON" : "OFF");
  }

  ESP_LOGI(TAG, "✅ V4L2 pipeline ready");
  ESP_LOGI(TAG, "   Device: %s", this->video_device_);
  ESP_LOGI(TAG, "   Resolution: %ux%u", this->width_, this->height_);
  ESP_LOGI(TAG, "   Target FPS: %.1f", 1000.0f / this->update_interval_);
  ESP_LOGI(TAG, "   Buffers: %d x %u bytes", VIDEO_BUFFER_COUNT, this->buffer_length_);
  ESP_LOGI(TAG, "   PPA: %s", (this->rotation_ != ROTATION_0 || this->mirror_x_ || this->mirror_y_) ? "ENABLED" : "DISABLED");
#else
  ESP_LOGE(TAG, "❌ V4L2 pipeline requires ESP32-P4");
  this->mark_failed();
#endif
}

#ifdef USE_ESP32_VARIANT_ESP32P4

bool LVGLCameraDisplay::open_v4l2_device_() {
  ESP_LOGI(TAG, "Opening V4L2 device: %s", this->video_device_);
  
  // Ouvrir le device video (comme M5Stack: O_RDONLY)
  this->video_fd_ = open(this->video_device_, O_RDONLY);
  if (this->video_fd_ < 0) {
    ESP_LOGE(TAG, "Failed to open %s: errno=%d", this->video_device_, errno);
    return false;
  }

  ESP_LOGI(TAG, "✅ V4L2 device opened (fd=%d)", this->video_fd_);

  // Query capabilities
  struct v4l2_capability cap;
  if (ioctl(this->video_fd_, VIDIOC_QUERYCAP, &cap) < 0) {
    ESP_LOGE(TAG, "VIDIOC_QUERYCAP failed");
    close(this->video_fd_);
    this->video_fd_ = -1;
    return false;
  }

  ESP_LOGI(TAG, "V4L2 Capabilities:");
  ESP_LOGI(TAG, "  Driver: %s", cap.driver);
  ESP_LOGI(TAG, "  Card: %s", cap.card);
  ESP_LOGI(TAG, "  Version: %u.%u.%u",
           (cap.version >> 16) & 0xFF,
           (cap.version >> 8) & 0xFF,
           cap.version & 0xFF);

  if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    ESP_LOGE(TAG, "Device does not support video capture");
    close(this->video_fd_);
    this->video_fd_ = -1;
    return false;
  }

  if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
    ESP_LOGE(TAG, "Device does not support streaming");
    close(this->video_fd_);
    this->video_fd_ = -1;
    return false;
  }

  return true;
}

bool LVGLCameraDisplay::setup_v4l2_format_() {
  ESP_LOGI(TAG, "Setting V4L2 format: %ux%u RGB565", this->width_, this->height_);

  // D'abord récupérer le format actuel
  struct v4l2_format fmt = {};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  
  if (ioctl(this->video_fd_, VIDIOC_G_FMT, &fmt) < 0) {
    ESP_LOGE(TAG, "VIDIOC_G_FMT failed");
    return false;
  }

  ESP_LOGI(TAG, "Current format: %ux%u", fmt.fmt.pix.width, fmt.fmt.pix.height);

  // Si le format n'est pas déjà RGB565, le configurer
  if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565 ||
      fmt.fmt.pix.width != this->width_ ||
      fmt.fmt.pix.height != this->height_) {
    
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = this->width_;
    fmt.fmt.pix.height = this->height_;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(this->video_fd_, VIDIOC_S_FMT, &fmt) < 0) {
      ESP_LOGE(TAG, "VIDIOC_S_FMT failed: errno=%d", errno);
      return false;
    }
  }

  // Vérifier le format final
  if (ioctl(this->video_fd_, VIDIOC_G_FMT, &fmt) < 0) {
    ESP_LOGE(TAG, "VIDIOC_G_FMT failed");
    return false;
  }

  this->buffer_length_ = fmt.fmt.pix.sizeimage;
  ESP_LOGI(TAG, "✅ V4L2 format set: %ux%u, buffer size=%u bytes",
           fmt.fmt.pix.width, fmt.fmt.pix.height, this->buffer_length_);

  return true;
}

bool LVGLCameraDisplay::setup_v4l2_buffers_() {
  ESP_LOGI(TAG, "Requesting %d V4L2 buffers...", VIDEO_BUFFER_COUNT);

  // Demander les buffers (comme M5Stack)
  struct v4l2_requestbuffers req = {};
  req.count = VIDEO_BUFFER_COUNT;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if (ioctl(this->video_fd_, VIDIOC_REQBUFS, &req) < 0) {
    ESP_LOGE(TAG, "VIDIOC_REQBUFS failed: errno=%d", errno);
    return false;
  }

  if (req.count < VIDEO_BUFFER_COUNT) {
    ESP_LOGW(TAG, "Only got %u buffers (requested %d)", req.count, VIDEO_BUFFER_COUNT);
  }

  ESP_LOGI(TAG, "Allocated %u buffers", req.count);

  // Mapper et queue les buffers (comme M5Stack)
  for (uint32_t i = 0; i < req.count; i++) {
    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;

    // Query buffer info
    if (ioctl(this->video_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
      ESP_LOGE(TAG, "VIDIOC_QUERYBUF failed for buffer %u", i);
      return false;
    }

    // Mapper le buffer (comme M5Stack)
    this->mmap_buffers_[i] = (uint8_t*)mmap(
      NULL,
      buf.length,
      PROT_READ | PROT_WRITE,
      MAP_SHARED,
      this->video_fd_,
      buf.m.offset
    );

    if (this->mmap_buffers_[i] == MAP_FAILED) {
      ESP_LOGE(TAG, "mmap failed for buffer %u: errno=%d", i, errno);
      return false;
    }

    ESP_LOGD(TAG, "Buffer %u: mapped at %p, length=%u, offset=%u",
             i, this->mmap_buffers_[i], buf.length, buf.m.offset);

    // Queue le buffer
    if (ioctl(this->video_fd_, VIDIOC_QBUF, &buf) < 0) {
      ESP_LOGE(TAG, "VIDIOC_QBUF failed for buffer %u", i);
      return false;
    }
  }

  ESP_LOGI(TAG, "✅ V4L2 buffers ready");
  return true;
}

bool LVGLCameraDisplay::start_v4l2_streaming_() {
  ESP_LOGI(TAG, "Starting V4L2 streaming...");

  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(this->video_fd_, VIDIOC_STREAMON, &type) < 0) {
    ESP_LOGE(TAG, "VIDIOC_STREAMON failed: errno=%d", errno);
    return false;
  }

  this->v4l2_streaming_ = true;
  ESP_LOGI(TAG, "✅ V4L2 streaming started");
  return true;
}

bool LVGLCameraDisplay::capture_v4l2_frame_(uint8_t **frame_data) {
  if (!this->v4l2_streaming_) {
    return false;
  }

  // Dequeue un buffer (comme M5Stack)
  struct v4l2_buffer buf = {};
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;

  if (ioctl(this->video_fd_, VIDIOC_DQBUF, &buf) < 0) {
    if (errno == EAGAIN) {
      // Pas de frame disponible
      return false;
    }
    ESP_LOGE(TAG, "VIDIOC_DQBUF failed: errno=%d", errno);
    return false;
  }

  // Récupérer le pointeur vers les données
  *frame_data = this->mmap_buffers_[buf.index];
  this->current_buffer_index_ = buf.index;

  return true;
}

void LVGLCameraDisplay::release_v4l2_frame_() {
  if (this->current_buffer_index_ >= 0) {
    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = this->current_buffer_index_;

    // Re-queue le buffer
    if (ioctl(this->video_fd_, VIDIOC_QBUF, &buf) < 0) {
      ESP_LOGW(TAG, "VIDIOC_QBUF failed");
    }
    
    this->current_buffer_index_ = -1;
  }
}

void LVGLCameraDisplay::cleanup_v4l2_() {
  if (this->v4l2_streaming_) {
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(this->video_fd_, VIDIOC_STREAMOFF, &type);
    this->v4l2_streaming_ = false;
  }

  // Unmap les buffers
  for (int i = 0; i < VIDEO_BUFFER_COUNT; i++) {
    if (this->mmap_buffers_[i] && this->mmap_buffers_[i] != MAP_FAILED) {
      munmap(this->mmap_buffers_[i], this->buffer_length_);
      this->mmap_buffers_[i] = nullptr;
    }
  }

  if (this->video_fd_ >= 0) {
    close(this->video_fd_);
    this->video_fd_ = -1;
  }

  this->deinit_ppa_();
}

bool LVGLCameraDisplay::init_ppa_() {
  ppa_client_config_t ppa_config = {
    .oper_type = PPA_OPERATION_SRM,
    .max_pending_trans_num = 1,
  };
  
  esp_err_t ret = ppa_register_client(&ppa_config, &this->ppa_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "PPA register failed: 0x%x", ret);
    return false;
  }

  uint16_t width = this->width_;
  uint16_t height = this->height_;
  
  // Ajuster dimensions selon rotation
  if (this->rotation_ == ROTATION_90 || this->rotation_ == ROTATION_270) {
    std::swap(width, height);
  }

  this->transform_buffer_size_ = width * height * 2;  // RGB565
  this->transform_buffer_ = (uint8_t*)heap_caps_aligned_alloc(
    64,
    this->transform_buffer_size_,
    MALLOC_CAP_SPIRAM
  );

  if (!this->transform_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate transform buffer");
    ppa_unregister_client(this->ppa_handle_);
    this->ppa_handle_ = nullptr;
    return false;
  }

  ESP_LOGI(TAG, "PPA transform buffer: %ux%u @ %u bytes", 
           width, height, this->transform_buffer_size_);
  
  return true;
}

void LVGLCameraDisplay::deinit_ppa_() {
  if (this->transform_buffer_) {
    heap_caps_free(this->transform_buffer_);
    this->transform_buffer_ = nullptr;
  }
  
  if (this->ppa_handle_) {
    ppa_unregister_client(this->ppa_handle_);
    this->ppa_handle_ = nullptr;
  }
}

bool LVGLCameraDisplay::transform_frame_(const uint8_t *src, uint8_t *dst) {
  if (!this->ppa_handle_ || !src || !dst) {
    return false;
  }

  // Configuration PPA identique à M5Stack
  ppa_srm_oper_config_t srm_config = {};
  
  srm_config.in.buffer = (void*)src;
  srm_config.in.pic_w = this->width_;
  srm_config.in.pic_h = this->height_;
  srm_config.in.block_w = this->width_;
  srm_config.in.block_h = this->height_;
  srm_config.in.block_offset_x = 0;
  srm_config.in.block_offset_y = 0;
  srm_config.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  
  uint16_t out_w = (this->rotation_ == ROTATION_90 || this->rotation_ == ROTATION_270) 
                   ? this->height_ : this->width_;
  uint16_t out_h = (this->rotation_ == ROTATION_90 || this->rotation_ == ROTATION_270)
                   ? this->width_ : this->height_;
  
  srm_config.out.buffer = dst;
  srm_config.out.buffer_size = this->transform_buffer_size_;
  srm_config.out.pic_w = out_w;
  srm_config.out.pic_h = out_h;
  srm_config.out.block_offset_x = 0;
  srm_config.out.block_offset_y = 0;
  srm_config.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  
  srm_config.rotation_angle = (ppa_srm_rotation_angle_t)this->rotation_;
  srm_config.scale_x = 1.0f;
  srm_config.scale_y = 1.0f;
  srm_config.mirror_x = this->mirror_x_;
  srm_config.mirror_y = this->mirror_y_;
  srm_config.rgb_swap = false;
  srm_config.byte_swap = false;
  srm_config.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
  srm_config.alpha_fix_val = 0xFF;
  srm_config.mode = PPA_TRANS_MODE_BLOCKING;
  
  esp_err_t ret = ppa_do_scale_rotate_mirror(this->ppa_handle_, &srm_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "PPA transform failed: 0x%x", ret);
    return false;
  }
  
  return true;
}

#endif

void LVGLCameraDisplay::loop() {
#ifdef USE_ESP32_VARIANT_ESP32P4
  if (!this->v4l2_streaming_) {
    return;
  }

  uint32_t now = millis();
  if (now - this->last_update_time_ < this->update_interval_) {
    return;
  }
  this->last_update_time_ = now;

  // Capturer une frame via V4L2 (comme M5Stack)
  uint8_t *frame_data = nullptr;
  if (!this->capture_v4l2_frame_(&frame_data)) {
    this->drop_count_++;
    return;
  }

  if (!frame_data) {
    return;
  }

  // Appliquer les transformations PPA si nécessaire
  uint8_t *display_buffer = frame_data;
  
  if (this->ppa_handle_ && this->transform_buffer_) {
    if (this->transform_frame_(frame_data, this->transform_buffer_)) {
      display_buffer = this->transform_buffer_;
    }
  }

  // Afficher sur le canvas LVGL (comme M5Stack)
  if (this->canvas_obj_) {
    uint16_t canvas_width = (this->rotation_ == ROTATION_90 || this->rotation_ == ROTATION_270)
                            ? this->height_ : this->width_;
    uint16_t canvas_height = (this->rotation_ == ROTATION_90 || this->rotation_ == ROTATION_270)
                             ? this->width_ : this->height_;

    lv_disp_t *disp = lv_obj_get_disp(this->canvas_obj_);
    if (disp) {
      _lv_disp_refr_timer(NULL);
    }

    lv_canvas_set_buffer(this->canvas_obj_, display_buffer, 
                         canvas_width, canvas_height, LV_IMG_CF_TRUE_COLOR);
    lv_obj_invalidate(this->canvas_obj_);
  }

  // Libérer le buffer V4L2
  this->release_v4l2_frame_();

  this->frame_count_++;

  // Log FPS
  if (this->first_update_) {
    this->first_update_ = false;
    this->last_fps_time_ = now;
  } else if (now - this->last_fps_time_ >= 5000) {
    float fps = this->frame_count_ * 1000.0f / (now - this->last_fps_time_);
    float drop_rate = (this->drop_count_ * 100.0f) / (this->frame_count_ + this->drop_count_);
    ESP_LOGI(TAG, "📊 Display: %.1f FPS | Drops: %u (%.1f%%)", 
             fps, this->drop_count_, drop_rate);
    this->frame_count_ = 0;
    this->drop_count_ = 0;
    this->last_fps_time_ = now;
  }
#endif
}

void LVGLCameraDisplay::dump_config() {
  ESP_LOGCONFIG(TAG, "LVGL Camera Display:");
  ESP_LOGCONFIG(TAG, "  Camera: %s", this->camera_ ? "Connected" : "Not connected");
  ESP_LOGCONFIG(TAG, "  Resolution: %ux%u", this->width_, this->height_);
  ESP_LOGCONFIG(TAG, "  Update interval: %u ms", this->update_interval_);
  ESP_LOGCONFIG(TAG, "  Rotation: %d°", this->rotation_);
  ESP_LOGCONFIG(TAG, "  Mirror X: %s", this->mirror_x_ ? "ON" : "OFF");
  ESP_LOGCONFIG(TAG, "  Mirror Y: %s", this->mirror_y_ ? "ON" : "OFF");
#ifdef USE_ESP32_VARIANT_ESP32P4
  ESP_LOGCONFIG(TAG, "  V4L2 Device: %s", this->video_device_);
  ESP_LOGCONFIG(TAG, "  V4L2 FD: %d", this->video_fd_);
  ESP_LOGCONFIG(TAG, "  PPA: %s", this->ppa_handle_ ? "Enabled" : "Disabled");
#endif
}

void LVGLCameraDisplay::configure_canvas(lv_obj_t *canvas) {
  this->canvas_obj_ = canvas;
  ESP_LOGI(TAG, "Canvas configured for camera display");
}

}  // namespace lvgl_camera_display
}  // namespace esphome
