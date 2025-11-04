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
#include <string.h>
#include <vector>

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

static inline bool wants_jpeg_(const std::string &fmt) {
  // Interprète "JPEG" ou "MJPEG" comme sortie encodeur JPEG.
  return (fmt == "JPEG" || fmt == "MJPEG");
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
  // Presets les plus courants + parse "WxH"
  if (res == "720P" || res == "1280x720")      { w = 1280; h = 720;  return true; }
  if (res == "1080P" || res == "1920x1080")    { w = 1920; h = 1080; return true; }
  if (res == "480P"  || res == "640x480" )     { w = 640;  h = 480;  return true; }
  if (res == "QVGA"  || res == "320x240" )     { w = 320;  h = 240;  return true; }
  if (res == "VGA"   || res == "640x480VGA")   { w = 640;  h = 480;  return true; }

  unsigned int pw=0, ph=0;
  if (sscanf(res.c_str(), "%ux%u", &pw, &ph) == 2 && pw>0 && ph>0) {
    w = pw; h = ph; return true;
  }
  return false;
}

static uint32_t map_pixfmt_fourcc_(const std::string &fmt) {
  // Formats bruts typiques côté ISP
  if (fmt == "RGB565") return V4L2_PIX_FMT_RGB565;
  if (fmt == "YUYV"  ) return V4L2_PIX_FMT_YUYV;
  if (fmt == "UYVY"  ) return V4L2_PIX_FMT_UYVY;
  if (fmt == "NV12"  ) return V4L2_PIX_FMT_NV12;

  // Formats compressés : rarement utilisés côté ISP, mais on gère MJPEG/JPEG
  if (fmt == "MJPEG" || fmt == "JPEG") return V4L2_PIX_FMT_MJPEG;

  // défaut raisonnable
  return V4L2_PIX_FMT_YUYV;
}

// Appliquer format + résolution + FPS sur le nœud ISP (/dev/video20)
static bool isp_apply_fmt_fps_(const std::string &res_s, const std::string &fmt_s, int fps) {
  int fd = -1;
  if (!open_node_(ESP_VIDEO_ISP1_DEVICE_NAME, &fd)) return false;

  uint32_t w=0, h=0;
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

// Appliquer qualité JPEG sur l’encodeur (/dev/video10)
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

// (Optionnel) Appliquer quelques paramètres H.264 si exposés (placeholder)
static bool h264_apply_basic_params_(int /*fps*/) {
  int fd = -1;
  if (!open_node_(ESP_VIDEO_H264_DEVICE_NAME, &fd)) return false;
  ESP_LOGI(TAG, "Encodeur H.264 ouvert pour configuration (aucun contrôle forcé par défaut).");
  close_fd_(fd);
  return true;
}

// ============================================================================
// Initialisation complète du pipeline vidéo
// ============================================================================
void MipiDSICamComponent::setup() {
  ESP_LOGI(TAG, "==============================");
  ESP_LOGI(TAG, " Initialisation MIPI-DSI-CAM");
  ESP_LOGI(TAG, "==============================");
  ESP_LOGI(TAG, "Capteur      : %d", this->sensor_type_);
  ESP_LOGI(TAG, "Horloge XCLK : %s @ %d Hz", this->xclk_pin_.c_str(), this->xclk_freq_);
  ESP_LOGI(TAG, "I2C Bus ID   : %d", this->i2c_id_);
  ESP_LOGI(TAG, "Adresse I2C  : 0x%02X", this->sensor_addr_);
  ESP_LOGI(TAG, "Résolution   : %s", this->resolution_.c_str());
  ESP_LOGI(TAG, "Format Pixel : %s", this->pixel_format_.c_str());
  ESP_LOGI(TAG, "Framerate    : %d FPS", this->framerate_);
  ESP_LOGI(TAG, "Qualité JPEG : %d", this->jpeg_quality_);

  esp_err_t err = ESP_OK;

  // --------------------------------------------------------------------------
  // Étape 0 : Initialisation du capteur caméra (esp_cam_sensor)
  // --------------------------------------------------------------------------
  this->sensor_dev_ = nullptr;
  err = esp_cam_sensor_init(&this->sensor_dev_);
  if (err != ESP_OK || this->sensor_dev_ == nullptr) {
    ESP_LOGE(TAG, "esp_cam_sensor_init() a échoué (err=0x%X)", err);
    return;
  }
  ESP_LOGI(TAG, "Capteur caméra initialisé (esp_cam_sensor).");

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
    return;
  }
  ESP_LOGI(TAG, "ESP-Video initialisé.");

  // --------------------------------------------------------------------------
  // Étape 2 : Créer devices vidéo (CSI source, ISP, encodeur)
  // --------------------------------------------------------------------------
  // 2.1 MIPI-CSI
  err = esp_video_create_csi_video_device(this->sensor_dev_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_video_create_csi_video_device() a échoué (err=0x%X)", err);
    goto FAIL_CLEANUP;
  }
  ESP_LOGI(TAG, "Device MIPI-CSI créé (source vidéo).");

  // 2.2 ISP
  err = esp_video_create_isp_video_device();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_video_create_isp_video_device() a échoué (err=0x%X)", err);
    goto FAIL_CLEANUP;
  }
  ESP_LOGI(TAG, "Device ISP créé.");

  // 2.3 Encodeur (H.264 ou JPEG)
  const bool use_jpeg = wants_jpeg_(this->pixel_format_);
  if (use_jpeg) {
    err = esp_video_create_jpeg_video_device(nullptr /*jpeg handle ou NULL*/);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Création encodeur JPEG échouée (err=0x%X)", err);
      goto FAIL_CLEANUP;
    }
    ESP_LOGI(TAG, "Encodeur JPEG créé.");
  } else {
    err = esp_video_create_h264_video_device(true /*hardware*/);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Création encodeur H.264 échouée (err=0x%X)", err);
      goto FAIL_CLEANUP;
    }
    ESP_LOGI(TAG, "Encodeur H.264 matériel créé.");
  }

  // --------------------------------------------------------------------------
  // Étape 3 : Activer le pipeline ISP (cam_dev="/dev/video0")
  // --------------------------------------------------------------------------
  memset(&this->isp_cfg_, 0, sizeof(this->isp_cfg_));
  this->isp_cfg_.cam_dev = ESP_VIDEO_MIPI_CSI_DEVICE_NAME;  // "/dev/video0"
  this->isp_cfg_.ipa_config = nullptr;                      // IPA par défaut

  err = esp_video_isp_pipeline_init(&this->isp_cfg_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_video_isp_pipeline_init() a échoué (err=0x%X)", err);
    goto FAIL_CLEANUP;
  }
  ESP_LOGI(TAG, "Pipeline ISP initialisé.");

  // --------------------------------------------------------------------------
  // Étape 4 : Appliquer format/résolution/FPS côté ISP (V4L2)
  // --------------------------------------------------------------------------
  if (!isp_apply_fmt_fps_(this->resolution_, this->pixel_format_, this->framerate_)) {
    ESP_LOGW(TAG, "Application V4L2 (format/résolution/FPS) sur ISP a échoué — le pipeline peut tout de même démarrer.");
  }

  // --------------------------------------------------------------------------
  // Étape 5 : Paramètres encodeur
  // --------------------------------------------------------------------------
  if (use_jpeg) {
    if (!jpeg_apply_quality_(this->jpeg_quality_)) {
      ESP_LOGW(TAG, "Qualité JPEG non appliquée (le flux peut quand même fonctionner).");
    }
  } else {
    (void)h264_apply_basic_params_(this->framerate_);
  }

  this->pipeline_started_ = true;
  ESP_LOGI(TAG, "Pipeline vidéo prêt. (Encodeur: %s)",
           use_jpeg ? "JPEG" : "H.264");
  return;

// --------------------------------------------------------------------------
// Nettoyage en cas d’échec d’une des étapes
// --------------------------------------------------------------------------
FAIL_CLEANUP:
  ESP_LOGW(TAG, "Nettoyage partiel du pipeline vidéo suite à une erreur...");
  if (wants_jpeg_(this->pixel_format_)) {
    (void)esp_video_destroy_jpeg_video_device();
  } else {
    (void)esp_video_destroy_h264_video_device(true /*hardware*/);
  }
  (void)esp_video_destroy_isp_video_device();
  (void)esp_video_destroy_csi_video_device();
  (void)esp_video_deinit();
  ESP_LOGE(TAG, "Initialisation MIPI-DSI-CAM interrompue.");
}

// ============================================================================
// Boucle principale (ajoute ta logique de capture/stream si besoin)
// ============================================================================
void MipiDSICamComponent::loop() {
  // TODO : capturer des frames, streamer, etc.
}

// ============================================================================
// Fonction de capture d’image (snapshot) -> carte SD
// ============================================================================
bool MipiDSICamComponent::capture_snapshot_to_file(const std::string &path) {
  const char *dev = ESP_VIDEO_JPEG_DEVICE_NAME;  // "/dev/video10"
  ESP_LOGI(TAG, "📸 Capture: %s → %s", dev, path.c_str());

  int fd = open(dev, O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    ESP_LOGE(TAG, "open(%s) a échoué: errno=%d (%s)", dev, errno, strerror(errno));
    return false;
  }

  // Obtenir la taille du buffer via G_FMT (si dispo)
  struct v4l2_format fmt;
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0) {
    ESP_LOGW(TAG, "VIDIOC_G_FMT échoué, taille par défaut 512k");
    fmt.fmt.pix.sizeimage = 512 * 1024;
  }

  // Lire une frame brute depuis le device
  std::vector<uint8_t> buffer(fmt.fmt.pix.sizeimage);
  ssize_t bytes_read = read(fd, buffer.data(), buffer.size());
  if (bytes_read <= 0) {
    ESP_LOGE(TAG, "read(%s) a échoué: errno=%d (%s)", dev, errno, strerror(errno));
    close(fd);
    return false;
  }
  ESP_LOGI(TAG, "Lu %d octets depuis le device", (int)bytes_read);
  close(fd);

  // Sauvegarder sur la carte SD (ou tout VFS monté)
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) {
    ESP_LOGE(TAG, "fopen(%s) pour écriture a échoué", path.c_str());
    return false;
  }
  size_t written = fwrite(buffer.data(), 1, bytes_read, f);
  fclose(f);

  if (written != (size_t)bytes_read) {
    ESP_LOGW(TAG, "Écriture incomplète (%u / %u octets)", (unsigned)written, (unsigned)bytes_read);
  }

  ESP_LOGI(TAG, "✅ Snapshot enregistré : %s (%u octets)", path.c_str(), (unsigned)written);
  return true;
}

}  // namespace mipi_dsi_cam
}  // namespace esphome
