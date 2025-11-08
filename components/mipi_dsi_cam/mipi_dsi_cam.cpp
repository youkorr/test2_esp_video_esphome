#include "mipi_dsi_cam.h"
#include "esphome/core/hal.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"

#include <string.h>
#include <vector>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>

// Headers C avec protection extern "C"
extern "C" {
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

namespace esphome {
namespace mipi_dsi_cam {

static const char *const TAG = "mipi_dsi_cam";

static constexpr uint32_t HEALTH_CHECK_INTERVAL_MS = 30000;
static constexpr size_t MAX_FRAME_SIZE = 512 * 1024;
static constexpr size_t MIN_FREE_HEAP = 100 * 1024;

static inline bool wants_jpeg_(const std::string &fmt) {
  return (fmt == "JPEG" || fmt == "MJPEG");
}

static inline bool wants_h264_(const std::string &fmt) {
  return (fmt == "H264");
}

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
  std::string res_upper = res;
  std::transform(res_upper.begin(), res_upper.end(), res_upper.begin(), ::toupper);
  
  if (res_upper == "QVGA")   { w = 320;  h = 240;  return true; }
  if (res_upper == "VGA")    { w = 640;  h = 480;  return true; }
  if (res_upper == "480P")   { w = 640;  h = 480;  return true; }
  if (res_upper == "720P")   { w = 1280; h = 720;  return true; }
  if (res_upper == "1080P")  { w = 1920; h = 1080; return true; }

  unsigned int pw = 0, ph = 0;
  if (sscanf(res.c_str(), "%ux%u", &pw, &ph) == 2 && pw > 0 && ph > 0) {
    w = pw; h = ph; return true;
  }
  
  return false;
}

static uint32_t map_pixfmt_fourcc_(const std::string &fmt) {
  if (fmt == "RGB565") return V4L2_PIX_FMT_RGB565;
  if (fmt == "YUYV")   return V4L2_PIX_FMT_YUYV;
  if (fmt == "UYVY")   return V4L2_PIX_FMT_UYVY;
  if (fmt == "NV12")   return V4L2_PIX_FMT_NV12;
  if (fmt == "MJPEG" || fmt == "JPEG") return V4L2_PIX_FMT_MJPEG;
  return V4L2_PIX_FMT_YUYV;
}

static bool isp_apply_fmt_fps_(const std::string &res_s, const std::string &fmt_s, int fps) {
  int fd = -1;
  if (!open_node_(ESP_VIDEO_ISP1_DEVICE_NAME, &fd)) return false;

  uint32_t w = 0, h = 0;
  if (!map_resolution_(res_s, w, h)) {
    ESP_LOGW(TAG, "Résolution '%s' non reconnue, fallback 1280x720", res_s.c_str());
    w = 1280; h = 720;
  }
  const uint32_t fourcc = map_pixfmt_fourcc_(fmt_s);

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

  if (fps > 0) {
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = fps;

    if (safe_ioctl_(fd, VIDIOC_S_PARM, &parm, "VIDIOC_S_PARM") < 0) {
      ESP_LOGW(TAG, "Impossible d'appliquer FPS=%d", fps);
    } else {
      ESP_LOGI(TAG, "ISP S_PARM: FPS=%d", fps);
    }
  }

  close_fd_(fd);
  return true;
}

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

  if (safe_ioctl_(fd, VIDIOC_S_CTRL, &ctrl, "VIDIOC_S_CTRL(JPEG_QUALITY)") < 0) {
    ESP_LOGW(TAG, "Impossible de régler la qualité JPEG");
  } else {
    ESP_LOGI(TAG, "Encodeur JPEG: Qualité=%d", quality);
  }

  close_fd_(fd);
  return true;
}

static bool h264_apply_basic_params_(int /*fps*/) {
  int fd = -1;
  if (!open_node_(ESP_VIDEO_H264_DEVICE_NAME, &fd)) return false;
  ESP_LOGI(TAG, "Encodeur H.264 ouvert pour configuration");
  close_fd_(fd);
  return true;
}

void MipiDSICamComponent::cleanup_pipeline_() {
  ESP_LOGW(TAG, "Nettoyage du pipeline vidéo...");
  // Le pipeline est géré par le composant esp_video
  this->pipeline_started_ = false;
  ESP_LOGI(TAG, "Pipeline vidéo marqué comme arrêté");
}

bool MipiDSICamComponent::check_pipeline_health_() {
  if (!this->pipeline_started_) {
    return false;
  }

  size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  if (free_heap < MIN_FREE_HEAP) {
    ESP_LOGW(TAG, "⚠️ Mémoire faible: %u octets libres (min: %u)", 
             (unsigned)free_heap, (unsigned)MIN_FREE_HEAP);
    this->error_count_++;
    return false;
  }

  return true;
}

void MipiDSICamComponent::setup() {
  ESP_LOGI(TAG, "==============================");
  ESP_LOGI(TAG, " Initialisation MIPI-DSI-CAM");
  ESP_LOGI(TAG, "==============================");
  ESP_LOGI(TAG, "Capteur      : %s", this->sensor_name_.c_str());
  ESP_LOGI(TAG, "Résolution   : %s", this->resolution_.c_str());
  ESP_LOGI(TAG, "Format Pixel : %s", this->pixel_format_.c_str());
  ESP_LOGI(TAG, "Framerate    : %d FPS", this->framerate_);
  ESP_LOGI(TAG, "Qualité JPEG : %d", this->jpeg_quality_);

  size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  ESP_LOGI(TAG, "Mémoire libre: %u octets", (unsigned)free_heap);

  if (free_heap < MIN_FREE_HEAP * 2) {
    ESP_LOGW(TAG, "⚠️ Mémoire faible pour l'initialisation (%u octets)", (unsigned)free_heap);
  }

  // Le pipeline ESP-Video est géré par le composant esp_video
  // Nous configurons seulement les paramètres V4L2
  ESP_LOGI(TAG, "✓ Pipeline ESP-Video géré par le composant esp_video");

  // Vérifier que les devices nécessaires sont disponibles
  bool isp_available = false;
  bool jpeg_available = false;
  bool h264_available = false;

  // Tester si l'ISP est disponible
  int test_fd = -1;
  if (open_node_(ESP_VIDEO_ISP1_DEVICE_NAME, &test_fd)) {
    isp_available = true;
    close_fd_(test_fd);
    ESP_LOGI(TAG, "✓ ISP détecté: %s", ESP_VIDEO_ISP1_DEVICE_NAME);
  } else {
    ESP_LOGW(TAG, "✗ ISP non disponible: %s", ESP_VIDEO_ISP1_DEVICE_NAME);
  }

  // Tester si JPEG est disponible
  test_fd = -1;
  if (open_node_(ESP_VIDEO_JPEG_DEVICE_NAME, &test_fd)) {
    jpeg_available = true;
    close_fd_(test_fd);
    ESP_LOGI(TAG, "✓ Encodeur JPEG détecté: %s", ESP_VIDEO_JPEG_DEVICE_NAME);
  } else {
    ESP_LOGW(TAG, "✗ Encodeur JPEG non disponible: %s", ESP_VIDEO_JPEG_DEVICE_NAME);
  }

  // Tester si H264 est disponible
  test_fd = -1;
  if (open_node_(ESP_VIDEO_H264_DEVICE_NAME, &test_fd)) {
    h264_available = true;
    close_fd_(test_fd);
    ESP_LOGI(TAG, "✓ Encodeur H.264 détecté: %s", ESP_VIDEO_H264_DEVICE_NAME);
  } else {
    ESP_LOGW(TAG, "✗ Encodeur H.264 non disponible: %s", ESP_VIDEO_H264_DEVICE_NAME);
  }

  // Vérifier qu'au moins un device est disponible
  if (!isp_available && !jpeg_available && !h264_available) {
    ESP_LOGE(TAG, "==============================");
    ESP_LOGE(TAG, "❌ ERREUR: Aucun device vidéo disponible!");
    ESP_LOGE(TAG, "==============================");
    ESP_LOGE(TAG, "Les devices suivants sont requis:");
    ESP_LOGE(TAG, "  - ISP: %s", ESP_VIDEO_ISP1_DEVICE_NAME);
    ESP_LOGE(TAG, "  - JPEG: %s", ESP_VIDEO_JPEG_DEVICE_NAME);
    ESP_LOGE(TAG, "  - H.264: %s", ESP_VIDEO_H264_DEVICE_NAME);
    ESP_LOGE(TAG, "");
    ESP_LOGE(TAG, "Vérifiez votre configuration esp_video:");
    ESP_LOGE(TAG, "  esp_video:");
    ESP_LOGE(TAG, "    enable_isp: true    # Requis pour RGB565/YUYV");
    ESP_LOGE(TAG, "    enable_jpeg: true   # Requis pour JPEG/MJPEG");
    ESP_LOGE(TAG, "    enable_h264: true   # Requis pour H.264");
    ESP_LOGE(TAG, "==============================");
    this->pipeline_started_ = false;
    this->mark_failed();
    return;
  }

  // L'ISP est configuré AUTOMATIQUEMENT par esp_video_init()
  // Pas besoin de configuration manuelle via VIDIOC_S_FMT sur /dev/video20
  // Le pipeline se configure quand on démarre le streaming sur /dev/video0
  if (isp_available && !wants_jpeg_(this->pixel_format_) && !wants_h264_(this->pixel_format_)) {
    ESP_LOGI(TAG, "✓ ISP sera utilisé automatiquement dans le pipeline de capture");
    ESP_LOGI(TAG, "  Format demandé: %s @ %s", this->pixel_format_.c_str(), this->resolution_.c_str());
  }

  // Configurer l'encodeur JPEG si nécessaire
  if (wants_jpeg_(this->pixel_format_)) {
    if (!jpeg_available) {
      ESP_LOGE(TAG, "❌ Format JPEG demandé mais encodeur JPEG non disponible");
      ESP_LOGE(TAG, "   Activez enable_jpeg: true dans esp_video");
      this->pipeline_started_ = false;
      this->mark_failed();
      return;
    }
    if (!jpeg_apply_quality_(this->jpeg_quality_)) {
      ESP_LOGW(TAG, "⚠️ Qualité JPEG non appliquée");
    } else {
      ESP_LOGI(TAG, "✓ Encodeur JPEG configuré (qualité: %d)", this->jpeg_quality_);
    }
  }

  // Configurer l'encodeur H264 si nécessaire
  if (wants_h264_(this->pixel_format_)) {
    if (!h264_available) {
      ESP_LOGE(TAG, "❌ Format H.264 demandé mais encodeur H.264 non disponible");
      ESP_LOGE(TAG, "   Activez enable_h264: true dans esp_video");
      this->pipeline_started_ = false;
      this->mark_failed();
      return;
    }
    (void)h264_apply_basic_params_(this->framerate_);
    ESP_LOGI(TAG, "✓ Encodeur H.264 configuré");
  }

  this->pipeline_started_ = true;
  this->last_health_check_ = millis();

  ESP_LOGI(TAG, "==============================");
  ESP_LOGI(TAG, "✅ Configuration caméra prête!");
  ESP_LOGI(TAG, "   Format: %s", this->pixel_format_.c_str());
  if (isp_available) ESP_LOGI(TAG, "   ISP: Disponible");
  if (jpeg_available) ESP_LOGI(TAG, "   JPEG: Disponible");
  if (h264_available) ESP_LOGI(TAG, "   H.264: Disponible");
  ESP_LOGI(TAG, "==============================");

  // Démarrer le streaming automatiquement pour LVGL display
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "🎬 Démarrage automatique du streaming vidéo continu...");
  if (this->start_streaming()) {
    ESP_LOGI(TAG, "✅ Streaming vidéo démarré avec succès!");
    ESP_LOGI(TAG, "   Le composant lvgl_camera_display peut maintenant afficher la vidéo");
  } else {
    ESP_LOGW(TAG, "⚠️ Échec du démarrage du streaming vidéo");
    ESP_LOGW(TAG, "   Le composant lvgl_camera_display ne pourra pas afficher de vidéo");
  }
}

void MipiDSICamComponent::loop() {
  if (!this->pipeline_started_) {
    return;
  }

  uint32_t now = millis();
  
  if (now - this->last_health_check_ >= HEALTH_CHECK_INTERVAL_MS) {
    this->last_health_check_ = now;
    
    if (!this->check_pipeline_health_()) {
      ESP_LOGW(TAG, "Vérification de santé du pipeline a échoué (erreurs: %u)", 
               (unsigned)this->error_count_);
      
      if (this->error_count_ > 5) {
        ESP_LOGE(TAG, "Trop d'erreurs détectées, nettoyage du pipeline...");
        this->cleanup_pipeline_();
        this->mark_failed();
      }
    } else {
      if (this->error_count_ > 0) {
        this->error_count_--;
      }
    }
  }
}

void MipiDSICamComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "MIPI DSI Camera:");
  ESP_LOGCONFIG(TAG, "  Capteur: %s", this->sensor_name_.c_str());
  ESP_LOGCONFIG(TAG, "  Résolution: %s", this->resolution_.c_str());
  ESP_LOGCONFIG(TAG, "  Format: %s", this->pixel_format_.c_str());
  ESP_LOGCONFIG(TAG, "  FPS: %d", this->framerate_);
  ESP_LOGCONFIG(TAG, "  État: %s", this->pipeline_started_ ? "ACTIF" : "INACTIF");
  ESP_LOGCONFIG(TAG, "  Snapshots: %u", (unsigned)this->snapshot_count_);
}

bool MipiDSICamComponent::capture_snapshot_to_file(const std::string &path) {
  if (!this->pipeline_started_) {
    ESP_LOGE(TAG, "Pipeline non démarré, impossible de capturer");
    return false;
  }

  size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  if (free_heap < MIN_FREE_HEAP + MAX_FRAME_SIZE) {
    ESP_LOGE(TAG, "Mémoire insuffisante pour capturer (%u octets libres)", (unsigned)free_heap);
    this->error_count_++;
    return false;
  }

  // Choisir le device de capture selon le format
  // IMPORTANT: Pour RGB565/YUYV/formats bruts, capturer depuis /dev/video0 (CSI)
  // L'ISP /dev/video20 est utilisé AUTOMATIQUEMENT dans le pipeline interne
  // Seulement JPEG/H264 utilisent leurs encodeurs dédiés
  const char *dev = wants_jpeg_(this->pixel_format_) ?
                    ESP_VIDEO_JPEG_DEVICE_NAME :       // /dev/video10 pour JPEG
                    wants_h264_(this->pixel_format_) ?
                    ESP_VIDEO_H264_DEVICE_NAME :       // /dev/video11 pour H264
                    ESP_VIDEO_MIPI_CSI_DEVICE_NAME;    // /dev/video0 pour RGB565/YUYV/etc

  ESP_LOGI(TAG, "📸 Capture V4L2 streaming: %s → %s", dev, path.c_str());

  // 1. Ouvrir le device
  int fd = open(dev, O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    ESP_LOGE(TAG, "open(%s) a échoué: errno=%d (%s)", dev, errno, strerror(errno));
    this->error_count_++;
    return false;
  }

  // 2. Vérifier le format actuel
  struct v4l2_format fmt;
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0) {
    ESP_LOGE(TAG, "VIDIOC_G_FMT a échoué: %s", strerror(errno));
    close(fd);
    this->error_count_++;
    return false;
  }

  ESP_LOGI(TAG, "Format actuel: %ux%u, fourcc=0x%08X, sizeimage=%u",
           fmt.fmt.pix.width, fmt.fmt.pix.height,
           fmt.fmt.pix.pixelformat, fmt.fmt.pix.sizeimage);

  // 3. Demander 2 buffers en mode MMAP
  struct v4l2_requestbuffers req;
  memset(&req, 0, sizeof(req));
  req.count = 2;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
    ESP_LOGE(TAG, "VIDIOC_REQBUFS a échoué: %s", strerror(errno));
    close(fd);
    this->error_count_++;
    return false;
  }

  ESP_LOGI(TAG, "✓ %u buffers alloués", req.count);

  // 4. Mapper et queuer les buffers
  struct {
    void *start;
    size_t length;
  } buffers[2];

  for (unsigned int i = 0; i < req.count; i++) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;

    // Obtenir les infos du buffer
    if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
      ESP_LOGE(TAG, "VIDIOC_QUERYBUF[%u] a échoué: %s", i, strerror(errno));
      close(fd);
      this->error_count_++;
      return false;
    }

    // Mapper le buffer en mémoire
    buffers[i].length = buf.length;
    buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                            MAP_SHARED, fd, buf.m.offset);

    if (buffers[i].start == MAP_FAILED) {
      ESP_LOGE(TAG, "mmap[%u] a échoué: %s", i, strerror(errno));
      // Nettoyer les buffers déjà mappés
      for (unsigned int j = 0; j < i; j++) {
        munmap(buffers[j].start, buffers[j].length);
      }
      close(fd);
      this->error_count_++;
      return false;
    }

    ESP_LOGI(TAG, "✓ Buffer[%u] mappé: %u octets @ %p", i, buf.length, buffers[i].start);

    // Mettre le buffer dans la queue
    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
      ESP_LOGE(TAG, "VIDIOC_QBUF[%u] a échoué: %s", i, strerror(errno));
      // Nettoyer tous les buffers mappés
      for (unsigned int j = 0; j <= i; j++) {
        munmap(buffers[j].start, buffers[j].length);
      }
      close(fd);
      this->error_count_++;
      return false;
    }
  }

  ESP_LOGI(TAG, "✓ Tous les buffers sont dans la queue");

  // 5. DÉMARRER LE STREAMING ★★★
  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
    ESP_LOGE(TAG, "❌ VIDIOC_STREAMON a échoué: %s", strerror(errno));
    // Nettoyer tous les buffers
    for (unsigned int i = 0; i < req.count; i++) {
      munmap(buffers[i].start, buffers[i].length);
    }
    close(fd);
    this->error_count_++;
    return false;
  }

  ESP_LOGI(TAG, "✅ STREAMING DÉMARRÉ - Le sensor stream maintenant !");
  ESP_LOGI(TAG, "   → CSI controller actif");
  ESP_LOGI(TAG, "   → ISP actif");
  ESP_LOGI(TAG, "   → Sensor SC202CS streaming MIPI data");

  // 6. Attendre et récupérer une frame
  struct v4l2_buffer buf;
  memset(&buf, 0, sizeof(buf));
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;

  ESP_LOGI(TAG, "Attente d'une frame...");

  if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
    ESP_LOGE(TAG, "VIDIOC_DQBUF a échoué: %s", strerror(errno));
    ioctl(fd, VIDIOC_STREAMOFF, &type);
    for (unsigned int i = 0; i < req.count; i++) {
      munmap(buffers[i].start, buffers[i].length);
    }
    close(fd);
    this->error_count_++;
    return false;
  }

  ESP_LOGI(TAG, "✅ Frame capturée: %u octets (buffer index=%u, sequence=%u)",
           buf.bytesused, buf.index, buf.sequence);

  // 7. Créer le répertoire si nécessaire
  std::string dir = path.substr(0, path.find_last_of('/'));
  if (!dir.empty()) {
    struct stat st;
    if (stat(dir.c_str(), &st) != 0) {
      mkdir(dir.c_str(), 0755);
    }
  }

  // 8. Sauvegarder la frame
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) {
    ESP_LOGE(TAG, "fopen(%s) pour écriture a échoué: %s", path.c_str(), strerror(errno));
    ioctl(fd, VIDIOC_STREAMOFF, &type);
    for (unsigned int i = 0; i < req.count; i++) {
      munmap(buffers[i].start, buffers[i].length);
    }
    close(fd);
    this->error_count_++;
    return false;
  }

  size_t written = fwrite(buffers[buf.index].start, 1, buf.bytesused, f);
  fclose(f);

  if (written != buf.bytesused) {
    ESP_LOGW(TAG, "Écriture incomplète (%u / %u octets)",
             (unsigned)written, buf.bytesused);
  }

  // 9. Arrêter le streaming
  if (ioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
    ESP_LOGW(TAG, "VIDIOC_STREAMOFF a échoué: %s", strerror(errno));
  } else {
    ESP_LOGI(TAG, "✓ Streaming arrêté");
  }

  // 10. Libérer les buffers mappés
  for (unsigned int i = 0; i < req.count; i++) {
    munmap(buffers[i].start, buffers[i].length);
  }

  close(fd);

  this->snapshot_count_++;
  ESP_LOGI(TAG, "✅ Snapshot #%u enregistré: %s (%u octets)",
           (unsigned)this->snapshot_count_, path.c_str(), (unsigned)written);

  return (written == buf.bytesused);
}

// ============================================================================
// Streaming Vidéo Continu pour LVGL Display
// ============================================================================

bool MipiDSICamComponent::start_streaming() {
  if (this->streaming_active_) {
    ESP_LOGW(TAG, "Streaming déjà actif");
    return true;
  }

  if (!this->pipeline_started_) {
    ESP_LOGE(TAG, "Pipeline non démarré, impossible de streamer");
    return false;
  }

  ESP_LOGI(TAG, "=== START STREAMING ===");

  // Choisir le device selon le format
  const char *dev = wants_jpeg_(this->pixel_format_) ?
                    ESP_VIDEO_JPEG_DEVICE_NAME :
                    wants_h264_(this->pixel_format_) ?
                    ESP_VIDEO_H264_DEVICE_NAME :
                    ESP_VIDEO_MIPI_CSI_DEVICE_NAME;

  ESP_LOGI(TAG, "Device: %s", dev);

  // 1. Ouvrir le device
  this->video_fd_ = open(dev, O_RDWR | O_NONBLOCK);
  if (this->video_fd_ < 0) {
    ESP_LOGE(TAG, "open(%s) failed: %s", dev, strerror(errno));
    return false;
  }

  // 2. Obtenir le format actuel
  struct v4l2_format fmt;
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (ioctl(this->video_fd_, VIDIOC_G_FMT, &fmt) < 0) {
    ESP_LOGE(TAG, "VIDIOC_G_FMT failed: %s", strerror(errno));
    close(this->video_fd_);
    this->video_fd_ = -1;
    return false;
  }

  this->image_width_ = fmt.fmt.pix.width;
  this->image_height_ = fmt.fmt.pix.height;
  this->image_buffer_size_ = fmt.fmt.pix.sizeimage;

  ESP_LOGI(TAG, "Format: %ux%u, fourcc=0x%08X, size=%u",
           this->image_width_, this->image_height_,
           fmt.fmt.pix.pixelformat, this->image_buffer_size_);

  // 3. Allouer le buffer d'image persistant
  this->image_buffer_ = (uint8_t*)heap_caps_malloc(this->image_buffer_size_, MALLOC_CAP_8BIT);
  if (!this->image_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate image buffer (%u bytes)", this->image_buffer_size_);
    close(this->video_fd_);
    this->video_fd_ = -1;
    return false;
  }

  memset(this->image_buffer_, 0, this->image_buffer_size_);
  ESP_LOGI(TAG, "✓ Image buffer allocated: %u bytes @ %p",
           this->image_buffer_size_, this->image_buffer_);

  // 4. Demander 2 buffers V4L2 en mode MMAP
  struct v4l2_requestbuffers req;
  memset(&req, 0, sizeof(req));
  req.count = 2;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if (ioctl(this->video_fd_, VIDIOC_REQBUFS, &req) < 0) {
    ESP_LOGE(TAG, "VIDIOC_REQBUFS failed: %s", strerror(errno));
    heap_caps_free(this->image_buffer_);
    this->image_buffer_ = nullptr;
    close(this->video_fd_);
    this->video_fd_ = -1;
    return false;
  }

  ESP_LOGI(TAG, "✓ %u V4L2 buffers requested", req.count);

  // 5. Mapper et queuer les buffers
  for (unsigned int i = 0; i < 2; i++) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;

    if (ioctl(this->video_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
      ESP_LOGE(TAG, "VIDIOC_QUERYBUF[%u] failed: %s", i, strerror(errno));
      this->stop_streaming();
      return false;
    }

    this->v4l2_buffers_[i].length = buf.length;
    this->v4l2_buffers_[i].start = mmap(NULL, buf.length,
                                        PROT_READ | PROT_WRITE,
                                        MAP_SHARED, this->video_fd_, buf.m.offset);

    if (this->v4l2_buffers_[i].start == MAP_FAILED) {
      ESP_LOGE(TAG, "mmap[%u] failed: %s", i, strerror(errno));
      this->stop_streaming();
      return false;
    }

    ESP_LOGI(TAG, "✓ Buffer[%u] mapped: %u bytes @ %p",
             i, buf.length, this->v4l2_buffers_[i].start);

    if (ioctl(this->video_fd_, VIDIOC_QBUF, &buf) < 0) {
      ESP_LOGE(TAG, "VIDIOC_QBUF[%u] failed: %s", i, strerror(errno));
      this->stop_streaming();
      return false;
    }
  }

  // 6. DÉMARRER LE STREAMING
  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(this->video_fd_, VIDIOC_STREAMON, &type) < 0) {
    ESP_LOGE(TAG, "VIDIOC_STREAMON failed: %s", strerror(errno));
    this->stop_streaming();
    return false;
  }

  this->streaming_active_ = true;
  this->frame_sequence_ = 0;

  ESP_LOGI(TAG, "✓ Streaming started");
  ESP_LOGI(TAG, "   → CSI controller active");
  ESP_LOGI(TAG, "   → ISP active");
  ESP_LOGI(TAG, "   → Sensor streaming MIPI data");

  return true;
}

bool MipiDSICamComponent::capture_frame() {
  if (!this->streaming_active_) {
    return false;
  }

  // 1. Dequeue un buffer rempli
  struct v4l2_buffer buf;
  memset(&buf, 0, sizeof(buf));
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;

  if (ioctl(this->video_fd_, VIDIOC_DQBUF, &buf) < 0) {
    if (errno == EAGAIN) {
      // Pas de frame disponible (mode non-blocking)
      return false;
    }
    ESP_LOGE(TAG, "VIDIOC_DQBUF failed: %s", strerror(errno));
    return false;
  }

  // 2. Copier les données dans le buffer d'image
  size_t copy_size = buf.bytesused < this->image_buffer_size_ ?
                     buf.bytesused : this->image_buffer_size_;

  memcpy(this->image_buffer_, this->v4l2_buffers_[buf.index].start, copy_size);

  this->frame_sequence_++;

  // Log uniquement la première frame
  if (this->frame_sequence_ == 1) {
    ESP_LOGI(TAG, "✅ First frame captured: %u bytes, sequence=%u",
             buf.bytesused, buf.sequence);
    ESP_LOGI(TAG, "   First pixels (RGB565): %02X%02X %02X%02X %02X%02X",
             this->image_buffer_[0], this->image_buffer_[1],
             this->image_buffer_[2], this->image_buffer_[3],
             this->image_buffer_[4], this->image_buffer_[5]);
  }

  // 3. Re-queue le buffer pour la prochaine capture
  if (ioctl(this->video_fd_, VIDIOC_QBUF, &buf) < 0) {
    ESP_LOGE(TAG, "VIDIOC_QBUF failed: %s", strerror(errno));
    return false;
  }

  return true;
}

void MipiDSICamComponent::stop_streaming() {
  if (!this->streaming_active_) {
    return;
  }

  ESP_LOGI(TAG, "=== STOP STREAMING ===");

  // 1. Arrêter le streaming
  if (this->video_fd_ >= 0) {
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(this->video_fd_, VIDIOC_STREAMOFF, &type) < 0) {
      ESP_LOGW(TAG, "VIDIOC_STREAMOFF failed: %s", strerror(errno));
    }
  }

  // 2. Libérer les buffers mappés
  for (int i = 0; i < 2; i++) {
    if (this->v4l2_buffers_[i].start != nullptr &&
        this->v4l2_buffers_[i].start != MAP_FAILED) {
      munmap(this->v4l2_buffers_[i].start, this->v4l2_buffers_[i].length);
      this->v4l2_buffers_[i].start = nullptr;
      this->v4l2_buffers_[i].length = 0;
    }
  }

  // 3. Fermer le device
  if (this->video_fd_ >= 0) {
    close(this->video_fd_);
    this->video_fd_ = -1;
  }

  // 4. Libérer le buffer d'image
  if (this->image_buffer_) {
    heap_caps_free(this->image_buffer_);
    this->image_buffer_ = nullptr;
  }

  this->streaming_active_ = false;
  this->image_width_ = 0;
  this->image_height_ = 0;
  this->image_buffer_size_ = 0;

  ESP_LOGI(TAG, "✓ Streaming stopped, resources freed");
}

}  // namespace mipi_dsi_cam
}  // namespace esphome




