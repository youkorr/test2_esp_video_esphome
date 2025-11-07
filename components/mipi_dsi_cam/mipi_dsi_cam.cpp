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

  // Setup sensor controls (exposition, gain, white balance, etc.)
  if (!this->setup_sensor_controls_()) {
    ESP_LOGW(TAG, "⚠️  Échec configuration contrôles capteur - image peut être sombre");
    // Continue quand même - pas critique
  }

  // Setup buffers
  if (!this->setup_buffers_()) {
    ESP_LOGE(TAG, "❌ Échec configuration buffers");
    this->mark_failed();
    return;
  }

  // Setup JPEG decoder si format JPEG (CRUCIAL pour décompression!)
  if (this->v4l2_pixelformat_ == V4L2_PIX_FMT_JPEG || this->v4l2_pixelformat_ == V4L2_PIX_FMT_MJPEG) {
    if (!this->setup_jpeg_decoder_()) {
      ESP_LOGE(TAG, "❌ Échec configuration décodeur JPEG");
      this->mark_failed();
      return;
    }
  } else {
    ESP_LOGI(TAG, "Format non-JPEG détecté - pas de décodeur JPEG nécessaire");
  }

  // Setup PPA (Pixel Processing Accelerator)
  if (!this->setup_ppa_()) {
    ESP_LOGE(TAG, "❌ Échec configuration PPA");
    this->mark_failed();
    return;
  }

  // Start streaming (optionnel)
  if (this->auto_start_) {
    ESP_LOGI(TAG, "Auto-start activé - démarrage du streaming...");
    if (!this->start_stream_()) {
      ESP_LOGE(TAG, "❌ Échec démarrage streaming");
      this->mark_failed();
      return;
    }
    this->streaming_ = true;
  } else {
    ESP_LOGI(TAG, "Auto-start désactivé - appelez start_streaming() manuellement");
  }

  this->initialized_ = true;

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

  // Set framerate - Méthode correcte: lire d'abord, puis modifier
  struct v4l2_streamparm parm;
  memset(&parm, 0, sizeof(parm));
  parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  // 1. Lire les paramètres actuels
  if (ioctl(this->video_fd_, VIDIOC_G_PARM, &parm) < 0) {
    ESP_LOGW(TAG, "VIDIOC_G_PARM failed: errno=%d (%s)", errno, strerror(errno));
  } else {
    ESP_LOGI(TAG, "Paramètres actuels:");
    ESP_LOGI(TAG, "  Capability: 0x%08X", parm.parm.capture.capability);
    ESP_LOGI(TAG, "  Capturemode: 0x%08X", parm.parm.capture.capturemode);
    ESP_LOGI(TAG, "  Timeperframe: %u/%u",
             parm.parm.capture.timeperframe.numerator,
             parm.parm.capture.timeperframe.denominator);

    // Vérifier si le framerate variable est supporté
    if (parm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) {
      ESP_LOGI(TAG, "  V4L2_CAP_TIMEPERFRAME: SUPPORTÉ ✓");
    } else {
      ESP_LOGW(TAG, "  V4L2_CAP_TIMEPERFRAME: NON SUPPORTÉ");
    }
  }

  // 2. Modifier le framerate
  parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  parm.parm.capture.timeperframe.numerator = 1;
  parm.parm.capture.timeperframe.denominator = this->framerate_;

  // 3. Écrire les nouveaux paramètres
  if (ioctl(this->video_fd_, VIDIOC_S_PARM, &parm) < 0) {
    ESP_LOGW(TAG, "VIDIOC_S_PARM failed: errno=%d (%s)", errno, strerror(errno));
    ESP_LOGW(TAG, "  Le driver ne supporte peut-être pas la configuration du framerate");
    ESP_LOGW(TAG, "  Le framerate sera contrôlé par le sensor (défaut: 30 FPS)");
  } else {
    // Le driver peut avoir ajusté la valeur, relire pour confirmer
    if (ioctl(this->video_fd_, VIDIOC_G_PARM, &parm) == 0) {
      uint32_t actual_fps = parm.parm.capture.timeperframe.denominator /
                            parm.parm.capture.timeperframe.numerator;
      ESP_LOGI(TAG, "✓ Framerate configuré: %u FPS (demandé: %u FPS)",
               actual_fps, this->framerate_);
    } else {
      ESP_LOGI(TAG, "✓ VIDIOC_S_PARM réussi (demandé: %u FPS)", this->framerate_);
    }
  }

  return true;
}

bool MipiDSICamComponent::setup_sensor_controls_() {
  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "Configuration contrôles capteur V4L2...");
  ESP_LOGI(TAG, "========================================");

  bool success = true;
  struct v4l2_control ctrl;

  // IMPORTANT: Ne PAS configurer gain/exposition fixes
  // L'IPA (Image Processing Algorithm) gère automatiquement:
  // - Auto White Balance (AWB)
  // - Auto Gain (via algorithmes IPA)
  // - Auto Exposure (via algorithmes IPA)
  // - Denoising, Sharpening, Gamma, Color Correction
  //
  // Les contrôles V4L2 standards (brightness, contrast, saturation)
  // ne fonctionnent pas sur SC202CS (errno=22 Invalid argument)
  // C'est NORMAL - l'ISP pipeline IPA gère tout ça

  ESP_LOGI(TAG, "  ✓ Gain/Exposition: gérés par IPA pipeline");
  ESP_LOGI(TAG, "  ✓ Auto White Balance: IPA 'awb.gray' actif");
  ESP_LOGI(TAG, "  ✓ Denoising: IPA 'denoising.gain_feedback' actif");
  ESP_LOGI(TAG, "  ✓ Sharpening: IPA 'sharpen.freq_feedback' actif");
  ESP_LOGI(TAG, "  ✓ Gamma: IPA 'gamma.lumma_feedback' actif");
  ESP_LOGI(TAG, "  ✓ Color Correction: IPA 'cc.linear' actif");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "  Note: Contrôles V4L2 (brightness, contrast, saturation)");
  ESP_LOGI(TAG, "        non utilisés - ISP pipeline gère tout automatiquement");

  ESP_LOGI(TAG, "========================================");
  if (success) {
    ESP_LOGI(TAG, "✅ Contrôles capteur configurés");
  } else {
    ESP_LOGW(TAG, "⚠️  Certains contrôles non supportés - vérifier capacités capteur");
  }
  ESP_LOGI(TAG, "========================================");

  return success;
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

bool MipiDSICamComponent::setup_jpeg_decoder_() {
  ESP_LOGI(TAG, "Configuration décodeur JPEG matériel...");

  // Allouer buffer pour le RGB565 décodé (même taille que output final)
  this->jpeg_decode_buffer_size_ = this->width_ * this->height_ * 2;  // RGB565 = 2 bytes/pixel
  this->jpeg_decode_buffer_ = (uint8_t*)heap_caps_calloc(
    this->jpeg_decode_buffer_size_, 1,
    MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM
  );

  if (this->jpeg_decode_buffer_ == nullptr) {
    ESP_LOGE(TAG, "❌ Échec allocation buffer décodage JPEG (%u octets)",
             (unsigned)this->jpeg_decode_buffer_size_);
    return false;
  }

  ESP_LOGI(TAG, "  Buffer décodage: %u octets (DMA+SPIRAM)", (unsigned)this->jpeg_decode_buffer_size_);

  // Configuration du décodeur JPEG
  jpeg_decode_engine_cfg_t decode_eng_cfg = {
    .timeout_ms = 100,  // Timeout 100ms pour le décodage
  };

  esp_err_t ret = jpeg_new_decoder_engine(&decode_eng_cfg, &this->jpeg_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "❌ jpeg_new_decoder_engine failed: %d", ret);
    heap_caps_free(this->jpeg_decode_buffer_);
    this->jpeg_decode_buffer_ = nullptr;
    return false;
  }

  ESP_LOGI(TAG, "✓ Décodeur JPEG matériel configuré");
  ESP_LOGI(TAG, "  Hardware accéléré: DCT, quantization, huffman");
  ESP_LOGI(TAG, "  Format sortie: RGB565");
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
  if (fmt_upper == "JPEG" || fmt_upper == "MJPEG") {
    ESP_LOGW(TAG, "JPEG demandé mais SC202CS ne supporte pas JPEG via MIPI-CSI");
    ESP_LOGW(TAG, "Utilisation RGB565 à la place");
    return V4L2_PIX_FMT_RGB565;
  }

  ESP_LOGW(TAG, "Format inconnu '%s', utilisation RGB565", fmt.c_str());
  return V4L2_PIX_FMT_RGB565;  // SC202CS capture en RGB565
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
    // Utiliser atoi au lieu de stoi (pas d'exceptions)
    int width_val = atoi(res.substr(0, pos).c_str());
    int height_val = atoi(res.substr(pos + 1).c_str());

    // Valider les valeurs
    if (width_val > 0 && height_val > 0 && width_val <= 4096 && height_val <= 4096) {
      w = static_cast<uint16_t>(width_val);
      h = static_cast<uint16_t>(height_val);
      return true;
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

// Tâche FreeRTOS dédiée pour la capture haute performance (comme M5Stack demo)
void camera_task_function(void* arg) {
  MipiDSICamComponent* camera = static_cast<MipiDSICamComponent*>(arg);

  ESP_LOGI(TAG, "🎬 Camera task démarrée sur Core %d", xPortGetCoreID());
  ESP_LOGI(TAG, "   Priority: %d", uxTaskPriorityGet(nullptr));

  uint32_t last_fps_log = 0;
  uint32_t total_dqbuf_time = 0;
  uint32_t total_jpeg_time = 0;
  uint32_t total_ppa_time = 0;
  uint32_t total_canvas_time = 0;
  uint32_t profile_count = 0;

  while (camera->task_running_) {
    if (!camera->streaming_ || camera->video_fd_ < 0 || camera->ppa_handle_ == nullptr) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    uint32_t t_start = millis();

    // DQBUF - Récupérer frame du driver
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(camera->video_fd_, VIDIOC_DQBUF, &buf) < 0) {
      if (errno != EAGAIN) {
        ESP_LOGE(TAG, "VIDIOC_DQBUF failed: errno=%d", errno);
      }
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    uint32_t t_dqbuf = millis();

    // Buffer source pour PPA (soit JPEG décodé, soit données brutes)
    uint8_t* source_buffer = camera->buffers_[buf.index];

    // DÉCODAGE JPEG si nécessaire (CRUCIAL pour décompression!)
    if (camera->jpeg_handle_ != nullptr) {
      // Configuration du décodage JPEG → RGB565
      jpeg_decode_cfg_t decode_cfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,  // BGR pour compatibilité ESP32-P4
      };

      uint32_t out_size = 0;

      // Décoder JPEG compressé → RGB565
      esp_err_t ret = jpeg_decoder_process(
        camera->jpeg_handle_,
        &decode_cfg,
        camera->buffers_[buf.index],  // JPEG compressé (entrée)
        buf.bytesused,                // Taille JPEG
        camera->jpeg_decode_buffer_,  // RGB565 décodé (sortie)
        camera->jpeg_decode_buffer_size_,
        &out_size                     // Taille de sortie
      );

      if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ jpeg_decoder_process failed: %d (JPEG=%u bytes)", ret, buf.bytesused);
        ioctl(camera->video_fd_, VIDIOC_QBUF, &buf);
        continue;
      }

      // Utiliser le buffer décodé pour PPA
      source_buffer = camera->jpeg_decode_buffer_;

      // Logger info JPEG toutes les 500 frames
      if (camera->frame_count_ % 500 == 0) {
        ESP_LOGI(TAG, "📸 JPEG: compressé=%u bytes, décodé=%u bytes, ratio=%.1fx",
                 buf.bytesused, out_size, (float)out_size / buf.bytesused);
      }
    }

    uint32_t t_jpeg = millis();

    // PPA - Scale/Rotate/Mirror sur données RGB565
    ppa_srm_oper_config_t srm_config = {
      .in = {
        .buffer = source_buffer,  // RGB565 (décodé ou brut)
        .pic_w = camera->width_,
        .pic_h = camera->height_,
        .block_w = camera->width_,
        .block_h = camera->height_,
        .block_offset_x = 0,
        .block_offset_y = 0,
        .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
      },
      .out = {
        .buffer = camera->output_buffer_,
        .buffer_size = camera->output_buffer_size_,
        .pic_w = camera->width_,
        .pic_h = camera->height_,
        .block_offset_x = 0,
        .block_offset_y = 0,
        .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
      },
      .rotation_angle = camera->map_rotation_(camera->rotation_angle_),
      .scale_x = 1.0f,
      .scale_y = 1.0f,
      .mirror_x = camera->mirror_x_,
      .mirror_y = camera->mirror_y_,
      .rgb_swap = false,
      .byte_swap = false,
      .mode = PPA_TRANS_MODE_BLOCKING,
    };

    esp_err_t ret = ppa_do_scale_rotate_mirror(camera->ppa_handle_, &srm_config);
    uint32_t t_ppa = millis();

    if (ret == ESP_OK && camera->canvas_ != nullptr) {
      // THREAD-SAFE: Ne PAS appeler lv_canvas_set_buffer() directement ici!
      // → Cause warning: "detected modifying dirty areas in render"
      //
      // Au lieu, on signale qu'un nouveau buffer est prêt.
      // La mise à jour du canvas se fera dans le contexte LVGL (loop() ou callback)
      camera->pending_frame_buffer_.store(camera->output_buffer_, std::memory_order_release);
      camera->new_frame_ready_ = true;

      camera->frame_count_++;
    }

    uint32_t t_canvas = millis();

    // QBUF - Retourner buffer au driver
    ioctl(camera->video_fd_, VIDIOC_QBUF, &buf);

    // Profiling détaillé
    total_dqbuf_time += (t_dqbuf - t_start);
    total_jpeg_time += (t_jpeg - t_dqbuf);
    total_ppa_time += (t_ppa - t_jpeg);
    total_canvas_time += (t_canvas - t_ppa);
    profile_count++;

    // Logger FPS et profiling toutes les 100 frames
    if (camera->frame_count_ % 100 == 0) {
      uint32_t now = millis();
      if (last_fps_log > 0) {
        float elapsed = (now - last_fps_log) / 1000.0f;
        float fps = 100.0f / elapsed;

        // Temps moyens
        uint32_t avg_dqbuf = total_dqbuf_time / profile_count;
        uint32_t avg_jpeg = total_jpeg_time / profile_count;
        uint32_t avg_ppa = total_ppa_time / profile_count;
        uint32_t avg_canvas = total_canvas_time / profile_count;

        ESP_LOGI(TAG, "🎞️ %u frames - FPS: %.2f", camera->frame_count_, fps);
        ESP_LOGI(TAG, "⏱️  Temps moyen: DQBUF=%ums, JPEG=%ums, PPA=%ums, Canvas=%ums",
                 avg_dqbuf, avg_jpeg, avg_ppa, avg_canvas);

        // Reset profiling
        total_dqbuf_time = 0;
        total_jpeg_time = 0;
        total_ppa_time = 0;
        total_canvas_time = 0;
        profile_count = 0;
      }
      last_fps_log = now;
    }

    // Délai court comme M5Stack (10ms)
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  ESP_LOGI(TAG, "🛑 Camera task arrêtée");
  camera->camera_task_handle_ = nullptr;
  vTaskDelete(nullptr);
}

bool MipiDSICamComponent::start_camera_task(lv_obj_t* canvas) {
  if (canvas == nullptr) {
    ESP_LOGE(TAG, "Canvas null - impossible de démarrer task");
    return false;
  }

  // Si la tâche est déjà en cours, mettre à jour le canvas seulement
  if (this->camera_task_handle_ != nullptr) {
    ESP_LOGI(TAG, "Camera task déjà active - mise à jour canvas");
    this->canvas_ = canvas;
    return true;
  }

  this->canvas_ = canvas;
  this->task_running_ = true;
  this->frame_count_ = 0;
  this->last_fps_time_ = 0;

  // Démarrer streaming si pas déjà fait
  if (!this->streaming_) {
    if (!this->start_streaming()) {
      ESP_LOGE(TAG, "Échec démarrage streaming");
      return false;
    }
  }

  // Créer tâche FreeRTOS sur Core 1 avec priorité 5 (comme M5Stack demo)
  BaseType_t result = xTaskCreatePinnedToCore(
    camera_task_function,           // Function
    "camera_task",                  // Name
    8 * 1024,                       // Stack size (8KB comme M5Stack)
    this,                           // Parameter
    5,                              // Priority (5 comme M5Stack)
    &this->camera_task_handle_,     // Handle
    1                               // Core 1
  );

  if (result != pdPASS) {
    ESP_LOGE(TAG, "❌ Échec création camera task");
    this->task_running_ = false;
    return false;
  }

  ESP_LOGI(TAG, "✅ Camera task démarrée (Core 1, Priority 5)");
  return true;
}

void MipiDSICamComponent::stop_camera_task() {
  if (this->camera_task_handle_ == nullptr) {
    return;
  }

  ESP_LOGI(TAG, "Arrêt camera task...");
  this->task_running_ = false;

  // Attendre que la tâche se termine (max 2 secondes)
  for (int i = 0; i < 20 && this->camera_task_handle_ != nullptr; i++) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (this->camera_task_handle_ != nullptr) {
    ESP_LOGW(TAG, "Task pas terminée après timeout - force delete");
    vTaskDelete(this->camera_task_handle_);
    this->camera_task_handle_ = nullptr;
  }

  this->canvas_ = nullptr;
}

void MipiDSICamComponent::update_canvas_if_ready() {
  // Vérifier si un nouveau buffer est prêt (thread-safe)
  if (!this->new_frame_ready_) {
    return;  // Pas de nouveau buffer
  }

  // Récupérer le buffer de manière atomique
  uint8_t* frame_buffer = this->pending_frame_buffer_.load(std::memory_order_acquire);

  if (frame_buffer == nullptr || this->canvas_ == nullptr) {
    return;
  }

  // IMPORTANT: Cette fonction DOIT être appelée depuis le contexte LVGL
  // (loop() ou callback LVGL) pour éviter le warning "modifying dirty areas in render"
  lv_canvas_set_buffer(this->canvas_, frame_buffer,
                      this->width_, this->height_, LV_IMG_CF_TRUE_COLOR);

  // Réinitialiser le flag (le prochain buffer sera signalé par camera_task)
  this->new_frame_ready_ = false;
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
