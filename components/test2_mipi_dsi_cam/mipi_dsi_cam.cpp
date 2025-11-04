// #ifdef USE_ESP32_VARIANT_ESP32P4   // <- décommente si tu utilises ce flag dans ton projet
#include "mipi_dsi_cam.h"

// ============================================================================
// Inclusions ESP-IDF
// ============================================================================
#ifdef __has_include
#  if __has_include("esp_err.h")
#    include "esp_err.h"
#  elif __has_include(<esp_err.h>)
#    include <esp_err.h>
#  else
#    error "esp_err.h introuvable — assure-toi d'avoir le framework ESP-IDF complet"
#  endif
#else
#  include "esp_err.h"
#endif

#include "esp_log.h"
#include "driver/gpio.h"

// POSIX / V4L2
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <vector>
#include <ctype.h>

// ============================================================================
// Espressif: alimentation & vidéo
// ============================================================================
extern "C" {
  #include "esp_ldo.h"
  #include "esp_cam_sensor.h"
  #include "esp_cam_sensor_types.h"
  #include "esp_video_init.h"
  #include "esp_video_device.h"
  #include "esp_video_ioctl.h"
  #include "esp_video_isp_ioctl.h"
  #include "esp_ipa.h"
  #include "esp_ipa_types.h"
  #include "linux/videodev2.h"
}

// Défauts si non fournis par headers
#ifndef ESP_VIDEO_MIPI_CSI_DEVICE_NAME
#define ESP_VIDEO_MIPI_CSI_DEVICE_NAME "/dev/video0"
#endif
#ifndef ESP_VIDEO_ISP1_DEVICE_NAME
#define ESP_VIDEO_ISP1_DEVICE_NAME     "/dev/video20"
#endif
#ifndef ESP_VIDEO_JPEG_DEVICE_NAME
#define ESP_VIDEO_JPEG_DEVICE_NAME     "/dev/video10"
#endif
#ifndef ESP_VIDEO_H264_DEVICE_NAME
#define ESP_VIDEO_H264_DEVICE_NAME     "/dev/video11"
#endif

namespace esphome {
namespace mipi_dsi_cam {

static const char *const TAG = "mipi_dsi_cam";

// ============================================================================
// Helpers
// ============================================================================
static inline bool wants_jpeg_(const std::string &fmt) {
  return (fmt == "JPEG" || fmt == "MJPEG");
}

static int parse_gpio_name_(const std::string &name) {
  if (name.empty()) return -1;
  const char *p = name.c_str();
  while (*p && isspace((unsigned char)*p)) p++;
  if (!strncasecmp(p, "GPIO", 4)) p += 4;
  while (*p && !isdigit((unsigned char)*p)) p++;
  return *p ? atoi(p) : -1;
}

static inline int safe_ioctl_(int fd, unsigned long req, void *arg, const char *req_name) {
  int r;
  do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
  if (r < 0) ESP_LOGE(TAG, "ioctl(%s) a échoué: errno=%d (%s)", req_name, errno, strerror(errno));
  return r;
}

static bool open_node_(const char *node, int *fd_out) {
  int fd = open(node, O_RDWR | O_NONBLOCK);
  if (fd < 0) { ESP_LOGE(TAG, "open(%s) échoué: %s", node, strerror(errno)); return false; }
  *fd_out = fd;
  return true;
}

static void close_fd_(int &fd) { if (fd >= 0) { close(fd); fd = -1; } }

static bool map_resolution_(const std::string &res, uint32_t &w, uint32_t &h) {
  if (res == "720P" || res == "1280x720")      { w=1280; h=720;  return true; }
  if (res == "1080P"|| res == "1920x1080")     { w=1920; h=1080; return true; }
  if (res == "480P" || res == "640x480")       { w=640;  h=480;  return true; }
  if (res == "QVGA" || res == "320x240")       { w=320;  h=240;  return true; }
  if (res == "VGA"  || res == "640x480VGA")    { w=640;  h=480;  return true; }
  unsigned int pw=0, ph=0;
  if (sscanf(res.c_str(), "%ux%u", &pw, &ph)==2 && pw && ph){ w=pw; h=ph; return true; }
  return false;
}

static uint32_t map_pixfmt_fourcc_(const std::string &fmt) {
  if (fmt == "RGB565") return V4L2_PIX_FMT_RGB565;
  if (fmt == "YUYV"  ) return V4L2_PIX_FMT_YUYV;
  if (fmt == "UYVY"  ) return V4L2_PIX_FMT_UYVY;
  if (fmt == "NV12"  ) return V4L2_PIX_FMT_NV12;
  if (fmt == "MJPEG" || fmt == "JPEG") return V4L2_PIX_FMT_MJPEG;
  return V4L2_PIX_FMT_YUYV;
}

// ============================================================================
// Membre: alimentation LDO du PHY MIPI (2.5V)
// ============================================================================
bool MipiDSICamComponent::init_ldo_() {
  ESP_LOGI(TAG, "Init LDO MIPI (2.5V)");

  esp_ldo_channel_config_t ldo_config = {
      .chan_id = 3,        // LDO3 => souvent utilisé pour MIPI
      .voltage_mv = 2500,  // adapte 1800/2500 selon ton design
  };

  esp_err_t ret = esp_ldo_acquire_channel(&ldo_config, &this->ldo_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esp_ldo_acquire_channel échoué: 0x%x", ret);
    return false;
  }

  ESP_LOGI(TAG, "LDO actif (2.5V) ✅");
  return true;
}

// ============================================================================
// ISP: appliquer format / résolution / FPS
// ============================================================================
static bool isp_apply_fmt_fps_(const std::string &res_s, const std::string &fmt_s, int fps) {
  int fd = -1;
  if (!open_node_(ESP_VIDEO_ISP1_DEVICE_NAME, &fd)) return false;

  uint32_t w=1280, h=720;
  if (!map_resolution_(res_s, w, h)) ESP_LOGW(TAG, "Résolution '%s' inconnue, fallback 1280x720", res_s.c_str());
  const uint32_t fourcc = map_pixfmt_fourcc_(fmt_s);

  struct v4l2_format fmt = {};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = w;
  fmt.fmt.pix.height = h;
  fmt.fmt.pix.pixelformat = fourcc;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;
  (void)safe_ioctl_(fd, VIDIOC_S_FMT, &fmt, "VIDIOC_S_FMT");
  ESP_LOGI(TAG, "ISP S_FMT -> %ux%u fourcc=0x%08X", fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.pixelformat);

  if (fps > 0) {
    struct v4l2_streamparm parm = {};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = fps;
    (void)safe_ioctl_(fd, VIDIOC_S_PARM, &parm, "VIDIOC_S_PARM");
    ESP_LOGI(TAG, "ISP S_PARM -> %dfps", fps);
  }

  close_fd_(fd);
  return true;
}

// ============================================================================
// JPEG: appliquer la qualité
// ============================================================================
static bool jpeg_apply_quality_(int quality) {
  int fd = open(ESP_VIDEO_JPEG_DEVICE_NAME, O_RDWR | O_NONBLOCK);
  if (fd < 0) { ESP_LOGE(TAG, "open(%s) échoué: %s", ESP_VIDEO_JPEG_DEVICE_NAME, strerror(errno)); return false; }

#ifndef V4L2_CID_JPEG_COMPRESSION_QUALITY
#define V4L2_CID_JPEG_COMPRESSION_QUALITY (V4L2_CID_JPEG_CLASS_BASE + 1)
#endif
  struct v4l2_control ctrl = {};
  ctrl.id = V4L2_CID_JPEG_COMPRESSION_QUALITY;
  ctrl.value = quality;
  if (safe_ioctl_(fd, VIDIOC_S_CTRL, &ctrl, "VIDIOC_S_CTRL(JPEG_QUALITY)") < 0) {
    ESP_LOGW(TAG, "Réglage qualité JPEG échoué (ctrl)");
  } else {
    ESP_LOGI(TAG, "Qualité JPEG appliquée = %d", quality);
  }

  close_fd_(fd);
  return true;
}

// ============================================================================
// Setup: pipeline complet
// ============================================================================
void MipiDSICamComponent::setup() {
  ESP_LOGI(TAG, "==============================");
  ESP_LOGI(TAG, " Initialisation MIPI-DSI-CAM ");
  ESP_LOGI(TAG, "==============================");
  ESP_LOGI(TAG, "Capteur: %s", this->sensor_name_.c_str());
  ESP_LOGI(TAG, "Bus I2C: %d | Addr: 0x%02X", this->i2c_id_, this->sensor_addr_);
  ESP_LOGI(TAG, "Lanes : %d", this->lane_);
  ESP_LOGI(TAG, "XCLK  : %s @ %d Hz", this->xclk_pin_.c_str(), this->xclk_freq_);
  ESP_LOGI(TAG, "Sortie: %s | %s @ %dfps (Q=%d)",
           wants_jpeg_(this->pixel_format_) ? "JPEG" : "H.264",
           this->resolution_.c_str(), this->framerate_, this->jpeg_quality_);

  // 1) Alimentation MIPI via LDO (2.5V)
  if (!this->init_ldo_()) {
    ESP_LOGE(TAG, "LDO MIPI non initialisé -> arrêt");
    return;
  }

  // 2) Init ESP-Video (CSI / SCCB)
  esp_video_init_csi_config_t csi_cfg = {};
  csi_cfg.sccb_config.init_sccb = false;       // on suppose I2C déjà dispo via BSP/ESPHome
  csi_cfg.reset_pin = (gpio_num_t)-1;
  csi_cfg.pwdn_pin  = (gpio_num_t)-1;

  memset(&this->init_cfg_, 0, sizeof(this->init_cfg_));
  this->init_cfg_.csi = &csi_cfg;

  esp_err_t err = esp_video_init(&this->init_cfg_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_video_init échoué: 0x%x", err);
    return;
  }

  // 3) Init capteur
  this->sensor_dev_ = nullptr;
  err = esp_cam_sensor_init(&this->sensor_dev_);
  if (err != ESP_OK || this->sensor_dev_ == nullptr) {
    ESP_LOGE(TAG, "esp_cam_sensor_init échoué: 0x%x", err);
    goto FAIL_DEINIT_VIDEO;
  }

  // 4) Créer devices (CSI, ISP, enc)
  if ((err = esp_video_create_csi_video_device(this->sensor_dev_)) != ESP_OK) {
    ESP_LOGE(TAG, "create CSI device échoué: 0x%x", err); goto FAIL_DEINIT_VIDEO;
  }
  if ((err = esp_video_create_isp_video_device()) != ESP_OK) {
    ESP_LOGE(TAG, "create ISP device échoué: 0x%x", err); goto FAIL_DESTROY_CSI;
  }
  if (wants_jpeg_(this->pixel_format_)) {
    if ((err = esp_video_create_jpeg_video_device(nullptr)) != ESP_OK) {
      ESP_LOGE(TAG, "create JPEG device échoué: 0x%x", err); goto FAIL_DESTROY_ISP;
    }
  } else {
    if ((err = esp_video_create_h264_video_device(true)) != ESP_OK) {
      ESP_LOGE(TAG, "create H264 device échoué: 0x%x", err); goto FAIL_DESTROY_ISP;
    }
  }

  // 5) Lier ISP au CSI
  memset(&this->isp_cfg_, 0, sizeof(this->isp_cfg_));
  this->isp_cfg_.cam_dev = ESP_VIDEO_MIPI_CSI_DEVICE_NAME; // "/dev/video0"
  this->isp_cfg_.ipa_config = nullptr;
  if ((err = esp_video_isp_pipeline_init(&this->isp_cfg_)) != ESP_OK) {
    ESP_LOGE(TAG, "isp_pipeline_init échoué: 0x%x", err);
    goto FAIL_DESTROY_CODEC;
  }

  // 6) Appliquer V4L2 (fmt/rés/fps) + qualité
  (void)isp_apply_fmt_fps_(this->resolution_, this->pixel_format_, this->framerate_);
  if (wants_jpeg_(this->pixel_format_)) (void)jpeg_apply_quality_(this->jpeg_quality_);

  this->pipeline_started_ = true;
  ESP_LOGI(TAG, "Pipeline vidéo prêt ✅");
  return;

// Nettoyages en cas d’échec
FAIL_DESTROY_CODEC:
  if (wants_jpeg_(this->pixel_format_)) (void)esp_video_destroy_jpeg_video_device();
  else (void)esp_video_destroy_h264_video_device(true);
FAIL_DESTROY_ISP:
  (void)esp_video_destroy_isp_video_device();
FAIL_DESTROY_CSI:
  (void)esp_video_destroy_csi_video_device();
FAIL_DEINIT_VIDEO:
  (void)esp_video_deinit();
  ESP_LOGE(TAG, "Initialisation caméra interrompue ❌");
}

// ============================================================================
// Loop: extension future (streaming / supervision)
// ============================================================================
void MipiDSICamComponent::loop() {
  // Rien pour le moment ; ajoute ton streaming si besoin.
}

// ============================================================================
// Snapshot: lit /dev/video10 (JPEG) et sauvegarde sur SD
// ============================================================================
bool MipiDSICamComponent::capture_snapshot_to_file(const std::string &path) {
  const char *dev = ESP_VIDEO_JPEG_DEVICE_NAME;  // "/dev/video10"
  int fd = open(dev, O_RDWR | O_NONBLOCK);
  if (fd < 0) { ESP_LOGE(TAG, "open(%s) échoué: %s", dev, strerror(errno)); return false; }

  struct v4l2_format fmt = {};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0) {
    ESP_LOGW(TAG, "VIDIOC_G_FMT échoué, taille par défaut 512k");
    fmt.fmt.pix.sizeimage = 512 * 1024;
  }
  size_t size = fmt.fmt.pix.sizeimage ? fmt.fmt.pix.sizeimage : (512 * 1024);

  std::vector<uint8_t> buf(size);
  ssize_t r = read(fd, buf.data(), buf.size());
  close(fd);
  if (r <= 0) { ESP_LOGE(TAG, "read() échoué: %s", strerror(errno)); return false; }

  FILE *f = fopen(path.c_str(), "wb");
  if (!f) { ESP_LOGE(TAG, "fopen(%s) échoué", path.c_str()); return false; }
  size_t w = fwrite(buf.data(), 1, r, f);
  fclose(f);

  if (w != (size_t)r) ESP_LOGW(TAG, "Ecriture partielle: %u / %u", (unsigned)w, (unsigned)r);
  ESP_LOGI(TAG, "✅ Snapshot sauvegardé: %s (%u octets)", path.c_str(), (unsigned)w);
  return true;
}

}  // namespace mipi_dsi_cam
}  // namespace esphome

// #endif // USE_ESP32_VARIANT_ESP32P4
