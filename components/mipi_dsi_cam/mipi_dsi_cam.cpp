#include "mipi_dsi_cam.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "driver/gpio.h"
#include "mipi_dsi_cam_drivers_generated.h"

#ifdef USE_ESP32_VARIANT_ESP32P4

namespace esphome {
namespace mipi_dsi_cam {

static const char *const TAG = "mipi_dsi_cam";

// ============================================================================
// SETUP - Architecture COMPLÈTE Tab5 avec PPA
// ============================================================================

void MipiDsiCam::setup() {
  ESP_LOGI(TAG, "═══════════════════════════════════════════════════");
  ESP_LOGI(TAG, "🎥 Init MIPI Camera avec ESP-Video + PPA (Tab5)");
  ESP_LOGI(TAG, "═══════════════════════════════════════════════════");
  ESP_LOGI(TAG, "  Sensor: %s", this->sensor_type_.c_str());
  ESP_LOGI(TAG, "  Resolution: %ux%u", this->width_, this->height_);
  ESP_LOGI(TAG, "  Format: %s", 
           this->pixel_format_ == PIXEL_FORMAT_RGB565 ? "RGB565" :
           this->pixel_format_ == PIXEL_FORMAT_JPEG ? "JPEG" :
           this->pixel_format_ == PIXEL_FORMAT_H264 ? "H264" : "YUV422");
  ESP_LOGI(TAG, "  Mirror: X=%s Y=%s", 
           this->mirror_x_ ? "ON" : "OFF",
           this->mirror_y_ ? "ON" : "OFF");
  
  // 1. Reset capteur
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(false);
    delay(10);
    this->reset_pin_->digital_write(true);
    delay(20);
  }
  
  delay(50);
  
  // 2. Driver capteur
  if (!this->create_sensor_driver_()) {
    ESP_LOGE(TAG, "❌ Driver creation failed");
    this->mark_failed();
    return;
  }
  
  // 3. Init capteur I2C
  if (!this->init_sensor_()) {
    ESP_LOGE(TAG, "❌ Sensor init failed");
    this->mark_failed();
    return;
  }
  
  // 4. Init ESP-Video
  if (!this->init_esp_video_()) {
    ESP_LOGE(TAG, "❌ ESP-Video init failed");
    this->mark_failed();
    return;
  }
  
  // 5. Ouvrir /dev/video0
  if (!this->open_video_device_()) {
    ESP_LOGE(TAG, "❌ Video device open failed");
    this->mark_failed();
    return;
  }
  
  // 6. Configurer format
  if (!this->configure_video_format_()) {
    ESP_LOGE(TAG, "❌ Format config failed");
    this->mark_failed();
    return;
  }
  
  // 7. Setup buffers V4L2
  if (!this->setup_video_buffers_()) {
    ESP_LOGE(TAG, "❌ Buffer setup failed");
    this->mark_failed();
    return;
  }
  
  // 8. Init PPA (NOUVEAU - comme Tab5)
  if (!this->init_ppa_()) {
    ESP_LOGE(TAG, "❌ PPA init failed");
    this->mark_failed();
    return;
  }
  
  // 9. Allouer buffer d'affichage (NOUVEAU - comme Tab5)
  if (!this->allocate_display_buffer_()) {
    ESP_LOGE(TAG, "❌ Display buffer alloc failed");
    this->mark_failed();
    return;
  }
  
  this->initialized_ = true;
  
  ESP_LOGI(TAG, "═══════════════════════════════════════════════════");
  ESP_LOGI(TAG, "✅ Camera ready");
  ESP_LOGI(TAG, "   Pipeline: Sensor→CSI→ESP-Video→ISP→V4L2→PPA→Display");
  ESP_LOGI(TAG, "   Device: /dev/video0 (fd=%d)", this->video_fd_);
  ESP_LOGI(TAG, "   Display buffer: %u bytes", this->display_buffer_size_);
  ESP_LOGI(TAG, "═══════════════════════════════════════════════════");
}

// ============================================================================
// Init Sensor (via driver)
// ============================================================================

bool MipiDsiCam::create_sensor_driver_() {
  ESP_LOGI(TAG, "Creating driver: %s", this->sensor_type_.c_str());
  
  this->sensor_driver_ = create_sensor_driver(this->sensor_type_, this);
  
  if (this->sensor_driver_ == nullptr) {
    ESP_LOGE(TAG, "Unknown sensor: %s", this->sensor_type_.c_str());
    return false;
  }
  
  ESP_LOGI(TAG, "Driver created: %s", this->sensor_driver_->get_name());
  return true;
}

bool MipiDsiCam::init_sensor_() {
  if (!this->sensor_driver_) {
    ESP_LOGE(TAG, "No sensor driver");
    return false;
  }
  
  ESP_LOGI(TAG, "Init sensor I2C: %s", this->sensor_driver_->get_name());
  
  uint16_t pid = 0;
  esp_err_t ret = this->sensor_driver_->read_id(&pid);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read sensor ID");
    return false;
  }
  
  if (pid != this->sensor_driver_->get_pid()) {
    ESP_LOGE(TAG, "Wrong PID: 0x%04X (expected 0x%04X)", 
             pid, this->sensor_driver_->get_pid());
    return false;
  }
  
  ESP_LOGI(TAG, "Sensor ID: 0x%04X ✓", pid);
  
  ret = this->sensor_driver_->init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Sensor init failed: %d", ret);
    return false;
  }
  
  ESP_LOGI(TAG, "Sensor initialized ✓");
  delay(200);
  return true;
}

// ============================================================================
// Init ESP-Video (comme Tab5)
// ============================================================================

bool MipiDsiCam::init_esp_video_() {
  ESP_LOGI(TAG, "Init ESP-Video...");

  // --- SCCB config (I2C du capteur, géré par ESPHome) ---
  esp_video_init_sccb_config_t sccb_config = {};
  sccb_config.init_sccb = false;
  sccb_config.i2c_handle = nullptr;  // Géré par ESPHome

  // --- MIPI-CSI config ---
  esp_video_init_mipi_csi_config_t csi_config = {};
  csi_config.sccb_config = sccb_config;

  // Remplace les -1 par GPIO_NUM_NC (non connectés)
  csi_config.reset_pin = GPIO_NUM_NC;
  csi_config.pwdn_pin  = GPIO_NUM_NC;

  // --- Config globale ---
  esp_video_init_config_t cam_config = {};
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
  cam_config.mipi_csi = &csi_config;  // Champ correct depuis ESP-Video 1.3+
#else
  cam_config.csi = &csi_config;
#endif

  // Ces champs n’existent plus dans ESP-Video >=1.3
  // cam_config.dvp = nullptr;
  // cam_config.jpeg = nullptr;

  // --- Initialisation du device ---
  esp_err_t ret = esp_video_init(ESP_VIDEO_MIPI_CSI_DEVICE_ID, &cam_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esp_video_init failed: %s", esp_err_to_name(ret));
    return false;
  }

  ESP_LOGI(TAG, "ESP-Video initialized ✓");
  return true;
}


// ============================================================================
// Open /dev/video0
// ============================================================================

bool MipiDsiCam::open_video_device_() {
  ESP_LOGI(TAG, "Opening /dev/video0...");
  
  this->video_fd_ = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
  if (this->video_fd_ < 0) {
    ESP_LOGE(TAG, "Cannot open %s: %d", ESP_VIDEO_MIPI_CSI_DEVICE_NAME, errno);
    return false;
  }
  
  struct v4l2_capability cap;
  if (ioctl(this->video_fd_, VIDIOC_QUERYCAP, &cap) != 0) {
    ESP_LOGE(TAG, "VIDIOC_QUERYCAP failed");
    close(this->video_fd_);
    this->video_fd_ = -1;
    return false;
  }
  
  ESP_LOGI(TAG, "Video device:");
  ESP_LOGI(TAG, "  Driver: %s", cap.driver);
  ESP_LOGI(TAG, "  Card: %s", cap.card);
  ESP_LOGI(TAG, "  Version: %d.%d.%d", 
           (cap.version >> 16) & 0xFF,
           (cap.version >> 8) & 0xFF,
           cap.version & 0xFF);
  
  return true;
}

// ============================================================================
// Configure Format
// ============================================================================

uint32_t MipiDsiCam::get_v4l2_pixformat_() const {
  switch (this->pixel_format_) {
    case PIXEL_FORMAT_RGB565: return V4L2_PIX_FMT_RGB565;
    case PIXEL_FORMAT_YUV422: return V4L2_PIX_FMT_YUV422P;
    case PIXEL_FORMAT_RAW8: return V4L2_PIX_FMT_SBGGR8;
    case PIXEL_FORMAT_JPEG: return V4L2_PIX_FMT_JPEG;
    case PIXEL_FORMAT_H264: return V4L2_PIX_FMT_H264;
    default: return V4L2_PIX_FMT_RGB565;
  }
}

bool MipiDsiCam::configure_video_format_() {
  ESP_LOGI(TAG, "Configuring format...");
  
  struct v4l2_format fmt = {};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = this->width_;
  fmt.fmt.pix.height = this->height_;
  fmt.fmt.pix.pixelformat = this->get_v4l2_pixformat_();
  fmt.fmt.pix.field = V4L2_FIELD_NONE;
  
  if (ioctl(this->video_fd_, VIDIOC_S_FMT, &fmt) < 0) {
    ESP_LOGE(TAG, "VIDIOC_S_FMT failed: %d", errno);
    return false;
  }
  
  if (ioctl(this->video_fd_, VIDIOC_G_FMT, &fmt) < 0) {
    ESP_LOGE(TAG, "VIDIOC_G_FMT failed");
    return false;
  }
  
  ESP_LOGI(TAG, "Format: %ux%u (0x%08X) = %u bytes/frame", 
           fmt.fmt.pix.width, fmt.fmt.pix.height,
           fmt.fmt.pix.pixelformat, fmt.fmt.pix.sizeimage);
  
  return true;
}

// ============================================================================
// Setup Buffers V4L2
// ============================================================================

bool MipiDsiCam::setup_video_buffers_() {
  ESP_LOGI(TAG, "Setup V4L2 buffers...");
  
  struct v4l2_requestbuffers req = {};
  req.count = this->buffer_count_;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;
  
  if (ioctl(this->video_fd_, VIDIOC_REQBUFS, &req) < 0) {
    ESP_LOGE(TAG, "VIDIOC_REQBUFS failed");
    return false;
  }
  
  this->buffer_count_ = req.count;
  
  this->v4l2_buffers_ = (struct v4l2_buffer*)calloc(
    this->buffer_count_, sizeof(struct v4l2_buffer)
  );
  this->buffer_mappings_ = (BufferMapping*)calloc(
    this->buffer_count_, sizeof(BufferMapping)
  );
  
  if (!this->v4l2_buffers_ || !this->buffer_mappings_) {
    ESP_LOGE(TAG, "Buffer alloc failed");
    return false;
  }
  
  for (uint32_t i = 0; i < this->buffer_count_; i++) {
    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;
    
    if (ioctl(this->video_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
      ESP_LOGE(TAG, "VIDIOC_QUERYBUF failed for buffer %u", i);
      return false;
    }
    
    void *start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                       MAP_SHARED, this->video_fd_, buf.m.offset);
    
    if (start == MAP_FAILED) {
      ESP_LOGE(TAG, "mmap failed for buffer %u", i);
      return false;
    }
    
    this->buffer_mappings_[i].start = start;
    this->buffer_mappings_[i].length = buf.length;
    
    if (ioctl(this->video_fd_, VIDIOC_QBUF, &buf) < 0) {
      ESP_LOGE(TAG, "VIDIOC_QBUF failed for buffer %u", i);
      return false;
    }
    
    this->v4l2_buffers_[i] = buf;
  }
  
  ESP_LOGI(TAG, "Buffers: %u x %u bytes", 
           this->buffer_count_, 
           this->buffer_mappings_[0].length);
  
  return true;
}

// ============================================================================
// Init PPA (NOUVEAU - comme Tab5)
// ============================================================================

ppa_srm_rotation_angle_t MipiDsiCam::get_ppa_rotation_() const {
  switch (this->rotation_angle_) {
    case 0: return PPA_SRM_ROTATION_ANGLE_0;
    case 90: return PPA_SRM_ROTATION_ANGLE_90;
    case 180: return PPA_SRM_ROTATION_ANGLE_180;
    case 270: return PPA_SRM_ROTATION_ANGLE_270;
    default: return PPA_SRM_ROTATION_ANGLE_0;
  }
}

bool MipiDsiCam::init_ppa_() {
  ESP_LOGI(TAG, "Init PPA (Pixel Processing Accelerator)...");
  
  ppa_client_config_t ppa_config = {};
  ppa_config.oper_type = PPA_OPERATION_SRM;  // Scale, Rotate, Mirror
  ppa_config.max_pending_trans_num = 1;
  
  esp_err_t ret = ppa_register_client(&ppa_config, &this->ppa_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "ppa_register_client failed: %s", esp_err_to_name(ret));
    return false;
  }
  
  ESP_LOGI(TAG, "PPA initialized ✓");
  return true;
}

// ============================================================================
// Allocate Display Buffer (NOUVEAU - comme Tab5)
// ============================================================================

bool MipiDsiCam::allocate_display_buffer_() {
  ESP_LOGI(TAG, "Allocating display buffer...");
  
  // Taille en bytes (RGB565 = 2 bytes/pixel)
  this->display_buffer_size_ = this->width_ * this->height_ * 2;
  
  // Allouer dans SPIRAM avec DMA capability (comme Tab5)
  this->display_buffer_ = (uint8_t*)heap_caps_calloc(
    this->display_buffer_size_, 1,
    MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM
  );
  
  if (!this->display_buffer_) {
    ESP_LOGE(TAG, "Display buffer alloc failed");
    return false;
  }
  
  ESP_LOGI(TAG, "Display buffer: %u bytes @ %p", 
           this->display_buffer_size_, 
           this->display_buffer_);
  
  return true;
}

// ============================================================================
// Start/Stop Streaming
// ============================================================================

bool MipiDsiCam::start_streaming() {
  if (!this->initialized_ || this->streaming_) {
    return this->streaming_;
  }
  
  ESP_LOGI(TAG, "Starting stream...");
  
  // Start sensor
  if (this->sensor_driver_) {
    esp_err_t ret = this->sensor_driver_->start_stream();
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Sensor start_stream: %d", ret);
    }
    delay(100);
  }
  
  // Start V4L2
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(this->video_fd_, VIDIOC_STREAMON, &type) < 0) {
    ESP_LOGE(TAG, "VIDIOC_STREAMON failed: %d", errno);
    return false;
  }
  
  this->streaming_ = true;
  this->total_frames_captured_ = 0;
  this->last_fps_report_time_ = millis();
  
  ESP_LOGI(TAG, "✅ Streaming @ %u FPS", this->framerate_);
  return true;
}

bool MipiDsiCam::stop_streaming() {
  if (!this->streaming_) {
    return true;
  }
  
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  ioctl(this->video_fd_, VIDIOC_STREAMOFF, &type);
  
  if (this->sensor_driver_) {
    this->sensor_driver_->stop_stream();
  }
  
  this->streaming_ = false;
  ESP_LOGI(TAG, "Streaming stopped");
  return true;
}

// ============================================================================
// Capture Frame avec PPA (NOUVEAU - comme Tab5)
// ============================================================================

bool MipiDsiCam::capture_frame() {
  if (!this->streaming_) {
    return false;
  }
  
  // Dequeue buffer V4L2
  struct v4l2_buffer buf = {};
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  
  if (ioctl(this->video_fd_, VIDIOC_DQBUF, &buf) < 0) {
    if (errno == EAGAIN) {
      return false;
    }
    ESP_LOGE(TAG, "VIDIOC_DQBUF failed: %d", errno);
    return false;
  }
  
  if (!(buf.flags & V4L2_BUF_FLAG_DONE) || buf.index >= this->buffer_count_) {
    ioctl(this->video_fd_, VIDIOC_QBUF, &buf);
    return false;
  }
  
  // Source = buffer V4L2
  uint8_t *source_buffer = (uint8_t*)this->buffer_mappings_[buf.index].start;
  
  // ★ PPA: Scale + Rotate + Mirror (comme Tab5)
  ppa_srm_oper_config_t srm_config = {};
  
  // Input (buffer V4L2)
  srm_config.in.buffer = source_buffer;
  srm_config.in.pic_w = this->width_;
  srm_config.in.pic_h = this->height_;
  srm_config.in.block_w = this->width_;
  srm_config.in.block_h = this->height_;
  srm_config.in.block_offset_x = 0;
  srm_config.in.block_offset_y = 0;
  srm_config.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  
  // Output (display buffer)
  srm_config.out.buffer = this->display_buffer_;
  srm_config.out.buffer_size = this->display_buffer_size_;
  srm_config.out.pic_w = this->width_;
  srm_config.out.pic_h = this->height_;
  srm_config.out.block_offset_x = 0;
  srm_config.out.block_offset_y = 0;
  srm_config.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  
  // Transformations
  srm_config.rotation_angle = this->get_ppa_rotation_();
  srm_config.scale_x = 1.0f;
  srm_config.scale_y = 1.0f;
  srm_config.mirror_x = this->mirror_x_;
  srm_config.mirror_y = this->mirror_y_;
  srm_config.rgb_swap = false;
  srm_config.byte_swap = false;
  srm_config.mode = PPA_TRANS_MODE_BLOCKING;
  
  // Appliquer PPA
  esp_err_t ret = ppa_do_scale_rotate_mirror(this->ppa_handle_, &srm_config);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "PPA failed: %s", esp_err_to_name(ret));
  }
  
  // Re-queue buffer V4L2
  ioctl(this->video_fd_, VIDIOC_QBUF, &buf);
  
  // Stats
  this->total_frames_captured_++;
  uint32_t now = millis();
  if (now - this->last_fps_report_time_ >= 5000) {
    float fps = (float)this->total_frames_captured_ / 
                ((now - this->last_fps_report_time_) / 1000.0f);
    ESP_LOGI(TAG, "📊 FPS: %.2f (%u frames)", fps, this->total_frames_captured_);
    this->total_frames_captured_ = 0;
    this->last_fps_report_time_ = now;
  }
  
  return true;
}

// ============================================================================
// Loop & Dump
// ============================================================================

void MipiDsiCam::loop() {
  // Rien - capture_frame() appelée par lvgl_camera_display
}

void MipiDsiCam::dump_config() {
  ESP_LOGCONFIG(TAG, "MIPI Camera (ESP-Video + PPA):");
  if (this->sensor_driver_) {
    ESP_LOGCONFIG(TAG, "  Sensor: %s (0x%04X)", 
                  this->sensor_driver_->get_name(),
                  this->sensor_driver_->get_pid());
  }
  ESP_LOGCONFIG(TAG, "  Resolution: %ux%u", this->width_, this->height_);
  ESP_LOGCONFIG(TAG, "  Format: %s", 
                this->pixel_format_ == PIXEL_FORMAT_RGB565 ? "RGB565" :
                this->pixel_format_ == PIXEL_FORMAT_JPEG ? "JPEG" :
                this->pixel_format_ == PIXEL_FORMAT_H264 ? "H264" : "YUV422");
  ESP_LOGCONFIG(TAG, "  PPA: Mirror X=%s Y=%s Rot=%u°",
                this->mirror_x_ ? "ON" : "OFF",
                this->mirror_y_ ? "ON" : "OFF",
                this->rotation_angle_);
  ESP_LOGCONFIG(TAG, "  Device: /dev/video0 (fd=%d)", this->video_fd_);
  ESP_LOGCONFIG(TAG, "  Streaming: %s", this->streaming_ ? "YES" : "NO");
}

}  // namespace mipi_dsi_cam
}  // namespace esphome

#endif  // USE_ESP32_VARIANT_ESP32P4
