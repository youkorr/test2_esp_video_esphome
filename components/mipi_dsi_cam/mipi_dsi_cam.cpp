#include "mipi_dsi_cam.h"

// ============================================================================
// Inclusions ESP-IDF sécurisées (ESP-IDF 5.4.2)
// ============================================================================
#ifdef __has_include
#  if __has_include("esp_err.h")
#    include "esp_err.h"
#  elif __has_include(<esp_err.h>)
#    include <esp_err.h>
#  else
#    error "esp_err.h introuvable — vérifie ton environnement ESP-IDF"
#  endif
#else
#  include "esp_err.h"
#endif

#ifdef __has_include
#  if __has_include("esp_log.h")
#    include "esp_log.h"
#  elif __has_include(<esp_log.h>)
#    include <esp_log.h>
#  endif
#endif

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <vector>
#include <sys/stat.h>

// POSIX pour open/ioctl/read/write
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

// ============================================================================
// Inclusions Espressif natives (présentes dans ton dépôt components/)
// ============================================================================
extern "C" {
  #include "esp_cam_sensor.h"
  #include "esp_cam_sensor_types.h"
  #include "esp_video_init.h"
  #include "esp_video_device.h"
  #include "esp_video_ioctl.h"
  #include "esp_video_isp_ioctl.h"
  #include "esp_ipa.h"
  #include "esp_ipa_types.h"
  #include "linux/videodev2.h"   // V4L2
}

namespace esphome {
namespace mipi_dsi_cam {

static const char *const TAG = "mipi_dsi_cam";

// Constantes
static constexpr uint32_t HEALTH_CHECK_INTERVAL_MS = 30000;  // 30 secondes
static constexpr size_t MAX_FRAME_SIZE = 512 * 1024;         // 512 KB
static constexpr size_t MIN_FREE_HEAP = 100 * 1024;          // 100 KB minimum

static inline bool wants_jpeg_(const std::string &fmt) {
  // Interprète "JPEG" ou "MJPEG" comme sortie encodeur JPEG.
  return (fmt == "JPEG" || fmt == "MJPEG");
}

static inline bool wants_h264_(const std::string &fmt) {
  return (fmt == "H264");
}

// ============================ Helpers généraux ============================

static inline int safe_ioctl_(int fd, unsigned long req, void *arg, const char *req_name) {
  int r;
  do {
    r = ioctl(fd, req, arg);
  } while (r == -1 && errno == EINTR);
  if (r < 0) {
    ESP_LOGE(TAG, "ioctl(%s) a échoué: errno=%d (%s)", req_name, errno, strerror(errno));
  }
  return r;
}

static bool open_node_(const char *node, int *fd_out) {
  int fd = open(node, O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    ESP_LOGE(TAG, "open(%s) a échoué: errno=%d (%s)", node, errno, strerror(errno));
    return false;
  }
  *fd_out = fd;
  ESP_LOGI(TAG, "Ouvert: %s (fd=%d)", node, fd);
  return true;
}

static void close_fd_(int &fd) {
  if (fd >= 0) {
    close(fd);
    fd = -1;
  }
}

static bool map_resolution_(const std::string &res, uint32_t &w, uint32_t &h) {
  // Presets les plus courants
  std::string res_upper = res;
  std::transform(res_upper.begin(), res_upper.end(), res_upper.begin(), ::toupper);
  
  if (res_upper == "QVGA")        { w = 320;  h = 240;  return true; }
  if (res_upper == "VGA")         { w = 640;  h = 480;  return true; }
  if (res_upper == "480P")        { w = 640;  h = 480;  return true; }
  if (res_upper == "720P")        { w = 1280; h = 720;  return true; }
  if (res_upper == "1080P")       { w = 1920; h = 1080; return true; }

  // Parse format WIDTHxHEIGHT (ex: 1280x720)
  unsigned int pw = 0, ph = 0;
  if (sscanf(res.c_str(), "%ux%u", &pw, &ph) == 2 && pw > 0 && ph > 0) {
    w = pw; 
    h = ph; 
    return true;
  }
  
  return false;
}

static uint32_t map_pixfmt_fourcc_(const std::string &fmt) {
  // Formats bruts typiques côté ISP
  if (fmt == "RGB565") return V4L2_PIX_FMT_RGB565;
  if (fmt == "YUYV"  ) return V4L2_PIX_FMT_YUYV;
  if (fmt == "UYVY"  ) return V4L2_PIX_FMT_UYVY;
  if (fmt == "NV12"  ) return V4L2_PIX_FMT_NV12;

  // Formats compressés
  if (fmt == "MJPEG" || fmt == "JPEG") return V4L2_PIX_FMT_MJPEG;

  // défaut raisonnable
  return V4L2_PIX_FMT_YUYV;
}

// Appliquer format + résolution + FPS sur le nœud ISP (/dev/video20)
static bool isp_apply_fmt_fps_(const std::string &res_s, const std::string &fmt_s, int fps) {
  int fd = -1;
  if (!open_node_(ESP_VIDEO_ISP1_DEVICE_NAME, &fd)) return false;

  uint32_t w = 0, h = 0;
  if (!map_resolution_(res_s, w, h)) {
    ESP_LOGW(TAG, "Résolution '%s' non reconnue, fallback 1280x720", res_s.c_str());
    w = 1280; h = 720;
  }
  const uint32_t fourcc = map_pixfmt_fourcc_(fmt_s);

  // S_FMT
  struct v4l2_format fmt;
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = w;
  fmt.fmt.pix.height = h;
  fmt.fmt.pix.pixelformat = fourcc;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;

  if (safe_ioctl_(fd, VIDIOC_S_FMT, &fmt, "VIDIOC_S_FMT") < 0) {
    close_fd_(fd);
    return false;
  }
  ESP_LOGI(TAG, "ISP S_FMT: %ux%u FOURCC=0x%08X", fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.pixelformat);

  // S_PARM (FPS)
  if (fps > 0) {
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = fps;

    if (safe_ioctl_(fd, VIDIOC_S_PARM, &parm, "VIDIOC_S_PARM") < 0) {
      ESP_LOGW(TAG, "Impossible d'appliquer FPS=%d via VIDIOC_S_PARM", fps);
      // pas bloquant
    } else {
      ESP_LOGI(TAG, "ISP S_PARM: FPS=%d", fps);
    }
  }

  close_fd_(fd);
  return true;
}

// Appliquer qualité JPEG sur l'encodeur (/dev/video10)
static bool jpeg_apply_quality_(int quality) {
  int fd = -1;
  if (!open_node_(ESP_VIDEO_JPEG_DEVICE_NAME, &fd)) return false;

#ifndef V4L2_CID_JPEG_COMPRESSION_QUALITY
#define V4L2_CID_JPEG_COMPRESSION_QUALITY (V4L2_CID_JPEG_CLASS_BASE+1)
#endif
  struct v4l2_control ctrl;
  memset(&ctrl, 0, sizeof(ctrl));
  ctrl.id = V4L2_CID_JPEG_COMPRESSION_QUALITY;
  ctrl.value = quality;

  if (safe_ioctl_(fd, VIDIOC_S_CTRL, &ctrl, "VIDIOC_S_CTRL(JPEG_COMPRESSION_QUALITY)") < 0) {
#ifdef V4L2_CID_JPEG_QUALITY
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_JPEG_QUALITY;
    ctrl.value = quality;
    if (safe_ioctl_(fd, VIDIOC_S_CTRL, &ctrl, "VIDIOC_S_CTRL(JPEG_QUALITY)") < 0) {
      ESP_LOGW(TAG, "Réglage qualité JPEG via S_CTRL a échoué, on tente EXT_CTRLS");
    } else {
      ESP_LOGI(TAG, "Encodeur JPEG: Qualité=%d (V4L2_CID_JPEG_QUALITY)", quality);
      close_fd_(fd);
      return true;
    }
#else
    ESP_LOGW(TAG, "Réglage qualité JPEG via S_CTRL a échoué, on tente EXT_CTRLS");
#endif

    struct v4l2_ext_control ec;
    memset(&ec, 0, sizeof(ec));
    ec.id = V4L2_CID_JPEG_COMPRESSION_QUALITY;
    ec.value = quality;

    struct v4l2_ext_controls ecs;
    memset(&ecs, 0, sizeof(ecs));
    ecs.which = V4L2_CTRL_ID2CLASS(ec.id);
    ecs.count = 1;
    ecs.controls = &ec;

    if (safe_ioctl_(fd, VIDIOC_S_EXT_CTRLS, &ecs, "VIDIOC_S_EXT_CTRLS(JPEG)") < 0) {
      ESP_LOGE(TAG, "Impossible de régler la qualité JPEG (toutes méthodes ont échoué)");
      close_fd_(fd);
      return false;
    }
    ESP_LOGI(TAG, "Encodeur JPEG: Qualité=%d (EXT_CTRLS)", quality);
  } else {
    ESP_LOGI(TAG, "Encodeur JPEG: Qualité=%d (S_CTRL)", quality);
  }

  close_fd_(fd);
  return true;
}

// (Optionnel) Appliquer quelques paramètres H.264 si exposés
static bool h264_apply_basic_params_(int /*fps*/) {
  int fd = -1;
  if (!open_node_(ESP_VIDEO_H264_DEVICE_NAME, &fd)) return false;
  ESP_LOGI(TAG, "Encodeur H.264 ouvert pour configuration (aucun contrôle forcé par défaut).");
  close_fd_(fd);
  return true;
}

// ============================================================================
// Nettoyage du pipeline
// ============================================================================
void MipiDSICamComponent::cleanup_pipeline_() {
  ESP_LOGW(TAG, "Nettoyage du pipeline vidéo...");
  
  if (wants_jpeg_(this->pixel_format_)) {
    esp_video_destroy_jpeg_video_device();
  } else if (wants_h264_(this->pixel_format_)) {
    esp_video_destroy_h264_video_device(true);
  }
  
  esp_video_destroy_isp_video_device();
  esp_video_destroy_csi_video_device();
  esp_video_deinit();
  
  this->pipeline_started_ = false;
  ESP_LOGI(TAG, "Pipeline vidéo nettoyé.");
}

// ============================================================================
// Vérification de santé du pipeline
// ============================================================================
bool MipiDSICamComponent::check_pipeline_health_() {
  if (!this->pipeline_started_) {
    return false;
  }

  // Vérifier la mémoire disponible
  size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  if (free_heap < MIN_FREE_HEAP) {
    ESP_LOGW(TAG, "⚠️ Mémoire faible: %u octets libres (min: %u)", 
             (unsigned)free_heap, (unsigned)MIN_FREE_HEAP);
    this->error_count_++;
    return false;
  }

  // TODO: Ajouter des vérifications spécifiques au pipeline si nécessaire
  // (ex: lire un statut depuis un device V4L2)

  return true;
}

// ============================================================================
// Initialisation complète du pipeline vidéo
// ============================================================================
void MipiDSICamComponent::setup() {
  ESP_LOGI(TAG, "==============================");
  ESP_LOGI(TAG, " Initialisation MIPI-DSI-CAM");
  ESP_LOGI(TAG, "==============================");
  ESP_LOGI(TAG, "Capteur      : %s", this->sensor_name_.c_str());
  ESP_LOGI(TAG, "Horloge XCLK : %s @ %d Hz", this->xclk_pin_.c_str(), this->xclk_freq_);
  ESP_LOGI(TAG, "I2C Bus ID   : %d", this->i2c_id_);
  ESP_LOGI(TAG, "Adresse I2C  : 0x%02X", this->sensor_addr_);
  ESP_LOGI(TAG, "Résolution   : %s", this->resolution_.c_str());
  ESP_LOGI(TAG, "Format Pixel : %s", this->pixel_format_.c_str());
  ESP_LOGI(TAG, "Framerate    : %d FPS", this->framerate_);
  ESP_LOGI(TAG, "Qualité JPEG : %d", this->jpeg_quality_);

  // Vérifier la mémoire disponible
  size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  ESP_LOGI(TAG, "Mémoire libre: %u octets", (unsigned)free_heap);
  
  if (free_heap < MIN_FREE_HEAP * 2) {
    ESP_LOGW(TAG, "⚠️ Mémoire faible pour l'initialisation (%u octets)", (unsigned)free_heap);
  }

  esp_err_t err = ESP_OK;

  // --------------------------------------------------------------------------
  // Étape 0 : Initialisation du capteur caméra (esp_cam_sensor)
  // --------------------------------------------------------------------------
  this->sensor_dev_ = nullptr;
  err = esp_cam_sensor_init(&this->sensor_dev_);
  if (err != ESP_OK || this->sensor_dev_ == nullptr) {
    ESP_LOGE(TAG, "esp_cam_sensor_init() a échoué (err=0x%X)", err);
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "✓ Capteur caméra initialisé (esp_cam_sensor).");

  // --------------------------------------------------------------------------
  // Étape 1 : Initialiser ESP-Video (CSI + SCCB/I2C config minimale)
  // --------------------------------------------------------------------------
  esp_video_init_csi_config_t csi_cfg = {};
  csi_cfg.sccb_config.init_sccb = false;   // I2C déjà géré (via BSP/ESPHome)
  csi_cfg.reset_pin = (gpio_num_t)-1;      // adapte si tu as reset/pwdn câblés
  csi_cfg.pwdn_pin  = (gpio_num_t)-1;

  memset(&this->init_cfg_, 0, sizeof(this->init_cfg_));
  this->init_cfg_.csi = &csi_cfg;

  err = esp_video_init(&this->init_cfg_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_video_init() a échoué (err=0x%X)", err);
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "✓ ESP-Video initialisé.");

  // --------------------------------------------------------------------------
  // Étape 2 : Créer devices vidéo (CSI source, ISP, encodeur)
  // --------------------------------------------------------------------------
  // 2.1 MIPI-CSI
  err = esp_video_create_csi_video_device(this->sensor_dev_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_video_create_csi_video_device() a échoué (err=0x%X)", err);
    goto FAIL_CLEANUP;
  }
  ESP_LOGI(TAG, "✓ Device MIPI-CSI créé (source vidéo).");

  // 2.2 ISP
  err = esp_video_create_isp_video_device();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_video_create_isp_video_device() a échoué (err=0x%X)", err);
    goto FAIL_CLEANUP;
  }
  ESP_LOGI(TAG, "✓ Device ISP créé.");

  // 2.3 Encodeur (H.264 ou JPEG)
  if (wants_jpeg_(this->pixel_format_)) {
    err = esp_video_create_jpeg_video_device(nullptr);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Création encodeur JPEG échouée (err=0x%X)", err);
      goto FAIL_CLEANUP;
    }
    ESP_LOGI(TAG, "✓ Encodeur JPEG créé.");
  } else if (wants_h264_(this->pixel_format_)) {
    err = esp_video_create_h264_video_device(true);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Création encodeur H.264 échouée (err=0x%X)", err);
      goto FAIL_CLEANUP;
    }
    ESP_LOGI(TAG, "✓ Encodeur H.264 matériel créé.");
  } else {
    ESP_LOGW(TAG, "Format '%s' ne nécessite pas d'encodeur dédié", this->pixel_format_.c_str());
  }

  // --------------------------------------------------------------------------
  // Étape 3 : Activer le pipeline ISP (cam_dev="/dev/video0")
  // --------------------------------------------------------------------------
  memset(&this->isp_cfg_, 0, sizeof(this->isp_cfg_));
  this->isp_cfg_.cam_dev = ESP_VIDEO_MIPI_CSI_DEVICE_NAME;
  this->isp_cfg_.ipa_config = nullptr;

  err = esp_video_isp_pipeline_init(&this->isp_cfg_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_video_isp_pipeline_init() a échoué (err=0x%X)", err);
    goto FAIL_CLEANUP;
  }
  ESP_LOGI(TAG, "✓ Pipeline ISP initialisé.");

  // --------------------------------------------------------------------------
  // Étape 4 : Appliquer format/résolution/FPS côté ISP (V4L2)
  // --------------------------------------------------------------------------
  if (!isp_apply_fmt_fps_(this->resolution_, this->pixel_format_, this->framerate_)) {
    ESP_LOGW(TAG, "⚠️ Application V4L2 (format/résolution/FPS) sur ISP a échoué");
  }

  // --------------------------------------------------------------------------
  // Étape 5 : Paramètres encodeur
  // --------------------------------------------------------------------------
  if (wants_jpeg_(this->pixel_format_)) {
    if (!jpeg_apply_quality_(this->jpeg_quality_)) {
      ESP_LOGW(TAG, "⚠️ Qualité JPEG non appliquée");
    }
  } else if (wants_h264_(this->pixel_format_)) {
    (void)h264_apply_basic_params_(this->framerate_);
  }

  this->pipeline_started_ = true;
  this->last_health_check_ = millis();
  
  ESP_LOGI(TAG, "==============================");
  ESP_LOGI(TAG, "✅ Pipeline vidéo prêt!");
  ESP_LOGI(TAG, "   Encodeur: %s", 
           wants_jpeg_(this->pixel_format_) ? "JPEG" : 
           wants_h264_(this->pixel_format_) ? "H.264" : "RAW");
  ESP_LOGI(TAG, "==============================");
  return;

FAIL_CLEANUP:
  this->cleanup_pipeline_();
  this->mark_failed();
  ESP_LOGE(TAG, "❌ Initialisation MIPI-DSI-CAM interrompue.");
}

// ============================================================================
// Boucle principale avec monitoring
// ============================================================================
void MipiDSICamComponent::loop() {
  if (!this->pipeline_started_) {
    return;
  }

  uint32_t now = millis();
  
  // Vérification périodique de santé
  if (now - this->last_health_check_ >= HEALTH_CHECK_INTERVAL_MS) {
    this->last_health_check_ = now;
    
    if (!this->check_pipeline_health_()) {
      ESP_LOGW(TAG, "Vérification de santé du pipeline a échoué (erreurs: %u)", 
               (unsigned)this->error_count_);
      
      // Si trop d'erreurs, tenter un redémarrage
      if (this->error_count_ > 5) {
        ESP_LOGE(TAG, "Trop d'erreurs détectées, nettoyage du pipeline...");
        this->cleanup_pipeline_();
        this->mark_failed();
      }
    } else {
      // Reset du compteur d'erreurs si tout va bien
      if (this->error_count_ > 0) {
        this->error_count_--;
      }
    }
  }
}

// ============================================================================
// Affichage de la configuration
// ============================================================================
void MipiDSICamComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "MIPI DSI Camera:");
  ESP_LOGCONFIG(TAG, "  Capteur: %s", this->sensor_name_.c_str());
  ESP_LOGCONFIG(TAG, "  Résolution: %s", this->resolution_.c_str());
  ESP_LOGCONFIG(TAG, "  Format: %s", this->pixel_format_.c_str());
  ESP_LOGCONFIG(TAG, "  FPS: %d", this->framerate_);
  ESP_LOGCONFIG(TAG, "  État: %s", this->pipeline_started_ ? "ACTIF" : "INACTIF");
  ESP_LOGCONFIG(TAG, "  Snapshots capturés: %u", (unsigned)this->snapshot_count_);
  
#ifdef USE_SD_CARD
  ESP_LOGCONFIG(TAG, "  Carte SD: %s", this->sd_card_ ? "Configurée" : "Non configurée");
#endif
}

// ============================================================================
// Fonction de capture d'image (snapshot) -> carte SD
// ============================================================================
bool MipiDSICamComponent::capture_snapshot_to_file(const std::string &path) {
  if (!this->pipeline_started_) {
    ESP_LOGE(TAG, "Pipeline non démarré, impossible de capturer");
    return false;
  }

#ifdef USE_SD_CARD
  if (!this->sd_card_) {
    ESP_LOGE(TAG, "Carte SD non configurée");
    return false;
  }
  
  // Vérifier que la carte SD est montée
  if (!this->sd_card_->is_mounted()) {
    ESP_LOGE(TAG, "Carte SD non montée");
    return false;
  }
#endif

  // Vérifier la mémoire disponible
  size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  if (free_heap < MIN_FREE_HEAP + MAX_FRAME_SIZE) {
    ESP_LOGE(TAG, "Mémoire insuffisante pour capturer (%u octets libres)", (unsigned)free_heap);
    this->error_count_++;
    return false;
  }

  const char *dev = wants_jpeg_(this->pixel_format_) ? 
                    ESP_VIDEO_JPEG_DEVICE_NAME : 
                    ESP_VIDEO_ISP1_DEVICE_NAME;
  
  ESP_LOGI(TAG, "📸 Capture: %s → %s", dev, path.c_str());

  int fd = open(dev, O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    ESP_LOGE(TAG, "open(%s) a échoué: errno=%d (%s)", dev, errno, strerror(errno));
    this->error_count_++;
    return false;
  }

  // Obtenir la taille du buffer via G_FMT
  struct v4l2_format fmt;
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  
  size_t buffer_size = MAX_FRAME_SIZE;
  if (ioctl(fd, VIDIOC_G_FMT, &fmt) >= 0 && fmt.fmt.pix.sizeimage > 0) {
    buffer_size = fmt.fmt.pix.sizeimage;
    ESP_LOGD(TAG, "Taille buffer depuis G_FMT: %u octets", (unsigned)buffer_size);
  } else {
    ESP_LOGW(TAG, "VIDIOC_G_FMT échoué, utilisation taille par défaut: %u", (unsigned)buffer_size);
  }

  // Limiter la taille pour éviter les problèmes mémoire
  if (buffer_size > MAX_FRAME_SIZE) {
    ESP_LOGW(TAG, "Taille buffer (%u) > MAX (%u), limitation appliquée", 
             (unsigned)buffer_size, (unsigned)MAX_FRAME_SIZE);
    buffer_size = MAX_FRAME_SIZE;
  }

  // Allocation dynamique avec vérification
  uint8_t *buffer = (uint8_t*)heap_caps_malloc(buffer_size, MALLOC_CAP_8BIT);
  if (!buffer) {
    ESP_LOGE(TAG, "Échec allocation mémoire (%u octets)", (unsigned)buffer_size);
    close(fd);
    this->error_count_++;
    return false;
  }

  // Lire une frame depuis le device
  ssize_t bytes_read = read(fd, buffer, buffer_size);
  close(fd);

  if (bytes_read <= 0) {
    ESP_LOGE(TAG, "read(%s) a échoué: errno=%d (%s)", dev, errno, strerror(errno));
    heap_caps_free(buffer);
    this->error_count_++;
    return false;
  }
  
  ESP_LOGI(TAG, "Lu %d octets depuis le device", (int)bytes_read);

  // Créer le répertoire parent si nécessaire
  std::string dir = path.substr(0, path.find_last_of('/'));
  if (!dir.empty()) {
    struct stat st;
    if (stat(dir.c_str(), &st) != 0) {
      // Le répertoire n'existe pas, le créer
      ESP_LOGD(TAG, "Création du répertoire: %s", dir.c_str());
      mkdir(dir.c_str(), 0755);
    }
  }

  // Sauvegarder sur la carte SD
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) {
    ESP_LOGE(TAG, "fopen(%s) pour écriture a échoué: %s", path.c_str(), strerror(errno));
    heap_caps_free(buffer);
    this->error_count_++;
    return false;
  }

  size_t written = fwrite(buffer, 1, bytes_read, f);
  fclose(f);
  heap_caps_free(buffer);

  if (written != (size_t)bytes_read) {
    ESP_LOGW(TAG, "Écriture incomplète (%u / %u octets)", (unsigned)written, (unsigned)bytes_read);
    this->error_count_++;
    return false;
  }

  this->snapshot_count_++;
  ESP_LOGI(TAG, "✅ Snapshot #%u enregistré: %s (%u octets)", 
           (unsigned)this->snapshot_count_, path.c_str(), (unsigned)written);
  
  return true;
}

}  // namespace mipi_dsi_cam
}  // namespace esphome







