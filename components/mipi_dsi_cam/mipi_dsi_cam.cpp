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
#include "esp_timer.h"  // Pour esp_timer_get_time() (profiling)
}

// imlib est optionnel - désactivé pour l'instant car compilé par ESP-IDF après PlatformIO
// Pour activer : ajouter -DENABLE_IMLIB_DRAWING dans build_flags
#ifdef ENABLE_IMLIB_DRAWING
  extern "C" {
    #include "imlib.h"
  }
  #define IMLIB_AVAILABLE 1
#else
  #define IMLIB_AVAILABLE 0
#endif

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
    // Silencieux, sauf si réellement utilisé (erreur rapportée par appelant si nécessaire)
    return false;
  }
  *fd_out = fd;
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

  if (fps > 0) {
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = fps;
    (void)safe_ioctl_(fd, VIDIOC_S_PARM, &parm, "VIDIOC_S_PARM");
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

  (void)safe_ioctl_(fd, VIDIOC_S_CTRL, &ctrl, "VIDIOC_S_CTRL(JPEG_QUALITY)");

  close_fd_(fd);
  return true;
}

static bool h264_apply_basic_params_(int /*fps*/) {
  int fd = -1;
  if (!open_node_(ESP_VIDEO_H264_DEVICE_NAME, &fd)) return false;
  close_fd_(fd);
  return true;
}

void MipiDSICamComponent::cleanup_pipeline_() {
  // Le pipeline est géré par le composant esp_video
  this->pipeline_started_ = false;
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
  // Vérifier mémoire disponible
  size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  if (free_heap < MIN_FREE_HEAP * 2) {
    ESP_LOGW(TAG, "⚠️ Low memory: %u bytes (min: %u)", (unsigned)free_heap, (unsigned)(MIN_FREE_HEAP * 2));
  }

  // Vérifier que les devices nécessaires sont disponibles
  bool isp_available = false;
  bool jpeg_available = false;
  bool h264_available = false;

  // Tester si l'ISP est disponible
  int test_fd = -1;
  if (open_node_(ESP_VIDEO_ISP1_DEVICE_NAME, &test_fd)) {
    isp_available = true;
    close_fd_(test_fd);
  }

  // Tester si JPEG est disponible
  test_fd = -1;
  if (open_node_(ESP_VIDEO_JPEG_DEVICE_NAME, &test_fd)) {
    jpeg_available = true;
    close_fd_(test_fd);
  }

  // Tester si H264 est disponible
  test_fd = -1;
  if (open_node_(ESP_VIDEO_H264_DEVICE_NAME, &test_fd)) {
    h264_available = true;
    close_fd_(test_fd);
  }

  // Vérifier qu'au moins un device est disponible
  if (!isp_available && !jpeg_available && !h264_available) {
    ESP_LOGE(TAG, "ERROR: No video devices available");
    ESP_LOGE(TAG, "  Required: ISP(%s), JPEG(%s), or H264(%s)",
             ESP_VIDEO_ISP1_DEVICE_NAME, ESP_VIDEO_JPEG_DEVICE_NAME, ESP_VIDEO_H264_DEVICE_NAME);
    ESP_LOGE(TAG, "  Enable in esp_video: enable_isp/enable_jpeg/enable_h264: true");
    this->pipeline_started_ = false;
    this->mark_failed();
    return;
  }

  // Configurer l'encodeur JPEG si nécessaire
  if (wants_jpeg_(this->pixel_format_)) {
    if (!jpeg_available) {
      ESP_LOGE(TAG, "ERROR: JPEG format requested but JPEG encoder not available (enable_jpeg: true)");
      this->pipeline_started_ = false;
      this->mark_failed();
      return;
    }
    if (!jpeg_apply_quality_(this->jpeg_quality_)) {
      ESP_LOGW(TAG, "WARNING: JPEG quality not applied");
    }
  }

  // Configurer l'encodeur H264 si nécessaire
  if (wants_h264_(this->pixel_format_)) {
    if (!h264_available) {
      ESP_LOGE(TAG, "ERROR: H264 format requested but H264 encoder not available (enable_h264: true)");
      this->pipeline_started_ = false;
      this->mark_failed();
      return;
    }
    (void)h264_apply_basic_params_(this->framerate_);
  }

  this->pipeline_started_ = true;
  this->last_health_check_ = millis();

  // Messages simples de succès
  ESP_LOGI(TAG, "esp-cam-sensor: ok (%s)", this->sensor_name_.c_str());
  if (isp_available) ESP_LOGI(TAG, "esp-video-isp: ok");
  if (jpeg_available) ESP_LOGI(TAG, "jpeg-encoder: ok");
  if (h264_available) ESP_LOGI(TAG, "h264-encoder: ok");
  ESP_LOGI(TAG, "Camera ready: %s @ %s (%d fps)",
           this->pixel_format_.c_str(), this->resolution_.c_str(), this->framerate_);
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

  // ESP_LOGI(TAG, "=== START STREAMING (Zero-Copy RGB565) ===");

  // Solution finale: Zero-copy pour 30+ FPS garanti
  // Utiliser les buffers V4L2 MMAP directement, sans copie PPA
  const char *dev = ESP_VIDEO_MIPI_CSI_DEVICE_NAME;  // /dev/video0

  // ESP_LOGI(TAG, "Device: %s (RGB565 zero-copy mode)", dev);
  // ESP_LOGW(TAG, "⚠️  Zero-copy mode: léger risque de tearing (généralement imperceptible)");

  // 1. Ouvrir le device
  this->video_fd_ = open(dev, O_RDWR | O_NONBLOCK);
  if (this->video_fd_ < 0) {
    ESP_LOGE(TAG, "open(%s) failed: %s", dev, strerror(errno));
    return false;
  }

  // 2. Configurer le format RGB565
  uint32_t width, height;
  if (!map_resolution_(this->resolution_, width, height)) {
    ESP_LOGE(TAG, "Invalid resolution: %s", this->resolution_.c_str());
    close(this->video_fd_);
    this->video_fd_ = -1;
    return false;
  }

  // RGB565 natif du CSI (pas de conversion, pas de copie)
  uint32_t fourcc = V4L2_PIX_FMT_RGB565;

  // Énumérer les formats supportés par le capteur (ESP-IDF 5.5.1 peut avoir des restrictions)
  ESP_LOGI(TAG, "Checking supported formats for %s...", this->sensor_name_.c_str());
  struct v4l2_fmtdesc fmtdesc;
  bool format_supported = false;
  for (int i = 0; i < 10; i++) {
    memset(&fmtdesc, 0, sizeof(fmtdesc));
    fmtdesc.index = i;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(this->video_fd_, VIDIOC_ENUM_FMT, &fmtdesc) < 0) {
      break;  // Pas d'autres formats
    }
    char fourcc_str[5];
    fourcc_str[0] = (fmtdesc.pixelformat >> 0) & 0xFF;
    fourcc_str[1] = (fmtdesc.pixelformat >> 8) & 0xFF;
    fourcc_str[2] = (fmtdesc.pixelformat >> 16) & 0xFF;
    fourcc_str[3] = (fmtdesc.pixelformat >> 24) & 0xFF;
    fourcc_str[4] = '\0';
    ESP_LOGI(TAG, "  Format[%d]: %s (%s)", i, fmtdesc.description, fourcc_str);
    if (fmtdesc.pixelformat == fourcc) {
      format_supported = true;
    }
  }

  if (!format_supported) {
    ESP_LOGW(TAG, "RGB565 may not be supported by sensor, trying anyway...");
  }

  // Énumérer les tailles de frame supportées pour RGB565
  ESP_LOGI(TAG, "Checking supported frame sizes for RGB565...");
  struct v4l2_frmsizeenum frmsize;
  bool size_found = false;
  for (int i = 0; i < 20; i++) {
    memset(&frmsize, 0, sizeof(frmsize));
    frmsize.index = i;
    frmsize.pixel_format = fourcc;
    if (ioctl(this->video_fd_, VIDIOC_ENUM_FRAMESIZES, &frmsize) < 0) {
      break;
    }
    if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
      ESP_LOGI(TAG, "  Size[%d]: %ux%u", i, frmsize.discrete.width, frmsize.discrete.height);
      if (frmsize.discrete.width == width && frmsize.discrete.height == height) {
        size_found = true;
      }
    }
  }

  if (!size_found) {
    ESP_LOGW(TAG, "⚠️  Requested size %ux%u not found in supported list", width, height);
    ESP_LOGW(TAG, "⚠️  Trying to set anyway (driver may adjust)...");
  }

  struct v4l2_format fmt;
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = width;
  fmt.fmt.pix.height = height;
  fmt.fmt.pix.pixelformat = fourcc;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;

  // SET le format pour que le driver calcule sizeimage
  if (ioctl(this->video_fd_, VIDIOC_S_FMT, &fmt) < 0) {
    ESP_LOGE(TAG, "VIDIOC_S_FMT failed: %s", strerror(errno));
    ESP_LOGE(TAG, "Requested: %ux%u RGB565", width, height);
    ESP_LOGE(TAG, "This may indicate:");
    ESP_LOGE(TAG, "  1. Sensor %s doesn't support this resolution in RGB565", this->sensor_name_.c_str());
    ESP_LOGE(TAG, "  2. ESP-IDF 5.5.1 has stricter format validation");
    ESP_LOGE(TAG, "  3. Try a different resolution (VGA/1080P) or pixel format");
    close(this->video_fd_);
    this->video_fd_ = -1;
    return false;
  }

  // 3. Vérifier le format appliqué (le driver peut ajuster)
  if (ioctl(this->video_fd_, VIDIOC_G_FMT, &fmt) < 0) {
    ESP_LOGE(TAG, "VIDIOC_G_FMT failed: %s", strerror(errno));
    close(this->video_fd_);
    this->video_fd_ = -1;
    return false;
  }

  this->image_width_ = fmt.fmt.pix.width;
  this->image_height_ = fmt.fmt.pix.height;
  // Note: fmt.fmt.pix.sizeimage peut retourner 0 avec certains drivers V4L2
  // La vraie taille sera récupérée des buffers V4L2 plus tard
  this->image_buffer_size_ = 0;

  // ESP_LOGI(TAG, "Format: %ux%u, RGB565",
  //          this->image_width_, this->image_height_);

  // 3. PAS d'allocation de buffer séparé - on utilise les buffers V4L2 directement (zero-copy)
  // image_buffer_ pointera vers le buffer V4L2 actif dans capture_frame()
  this->image_buffer_ = nullptr;
  // ESP_LOGI(TAG, "✓ Zero-copy mode: using V4L2 MMAP buffers directly (no PPA, no separate buffer)");

  // 4. Demander 2 buffers V4L2 en mode MMAP
  struct v4l2_requestbuffers req;
  memset(&req, 0, sizeof(req));
  req.count = 2;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if (ioctl(this->video_fd_, VIDIOC_REQBUFS, &req) < 0) {
    ESP_LOGE(TAG, "VIDIOC_REQBUFS failed: %s", strerror(errno));
    close(this->video_fd_);
    this->video_fd_ = -1;
    return false;
  }

  // ESP_LOGI(TAG, "✓ %u V4L2 buffers requested", req.count);

  // 7. Mapper et queuer les buffers
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

    // ESP_LOGI(TAG, "✓ Buffer[%u] mapped: %u bytes @ %p",
    //          i, buf.length, this->v4l2_buffers_[i].start);

    // Utiliser la taille réelle du buffer V4L2 (au lieu de fmt.fmt.pix.sizeimage qui peut être 0)
    if (i == 0) {
      this->image_buffer_size_ = buf.length;
      // ESP_LOGI(TAG, "✓ Image buffer size set to %u bytes (from V4L2 buffer)", this->image_buffer_size_);
    }

    if (ioctl(this->video_fd_, VIDIOC_QBUF, &buf) < 0) {
      ESP_LOGE(TAG, "VIDIOC_QBUF[%u] failed: %s", i, strerror(errno));
      this->stop_streaming();
      return false;
    }
  }

  // 8. DÉMARRER LE STREAMING
  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(this->video_fd_, VIDIOC_STREAMON, &type) < 0) {
    ESP_LOGE(TAG, "VIDIOC_STREAMON failed: %s", strerror(errno));
    this->stop_streaming();
    return false;
  }

  this->streaming_active_ = true;
  this->frame_sequence_ = 0;

  ESP_LOGI(TAG, "mipi_dsi_cam: streaming started");

  // Logs détaillés commentés pour réduire verbosité
  // ESP_LOGI(TAG, "   → CSI controller active");
  // ESP_LOGI(TAG, "   → ISP active");
  // ESP_LOGI(TAG, "   → Sensor streaming MIPI data");
  // ESP_LOGI(TAG, "   → Zero-copy: LVGL uses V4L2 buffers directly (no PPA, no copy)");
  //
  // // Test 2: Memory zone analysis (PPA performance investigation)
  // ESP_LOGI(TAG, "");
  // ESP_LOGI(TAG, "📍 Memory Zone Analysis (Test 2):");
  //
  // // Analyze V4L2 buffers
  // for (int i = 0; i < 2; i++) {
  //   uintptr_t addr = (uintptr_t)this->v4l2_buffers_[i].start;
  //   const char* zone = "UNKNOWN";
  //   if (addr >= 0x48000000 && addr < 0x4C000000) {
  //     zone = "SPIRAM (0x48000000-0x4C000000)";
  //   } else if (addr >= 0x40800000 && addr < 0x40900000) {
  //     zone = "SRAM (0x40800000-0x40900000)";
  //   } else if (addr >= 0x40000000 && addr < 0x40800000) {
  //     zone = "IRAM/DRAM";
  //   }
  //   ESP_LOGI(TAG, "   V4L2 buffer[%d]: %p → %s", i, this->v4l2_buffers_[i].start, zone);
  // }
  //
  // // Analyze image_buffer_
  // uintptr_t img_addr = (uintptr_t)this->image_buffer_;
  // const char* img_zone = "UNKNOWN";
  // if (img_addr >= 0x48000000 && img_addr < 0x4C000000) {
  //   img_zone = "SPIRAM (0x48000000-0x4C000000)";
  // } else if (img_addr >= 0x40800000 && img_addr < 0x40900000) {
  //   img_zone = "SRAM (0x40800000-0x40900000)";
  // } else if (img_addr >= 0x40000000 && addr < 0x40800000) {
  //   img_zone = "IRAM/DRAM";
  // }
  // ESP_LOGI(TAG, "   image_buffer_: %p → %s", this->image_buffer_, img_zone);
  //
  // ESP_LOGI(TAG, "");
  // ESP_LOGI(TAG, "💡 PPA Performance Notes:");
  // ESP_LOGI(TAG, "   - PPA DMA should work efficiently on SPIRAM with DMA capability");
  // ESP_LOGI(TAG, "   - Expected PPA bandwidth: >100 MB/s");
  // ESP_LOGI(TAG, "   - Current observed: ~42 MB/s (investigating why)");
  // ESP_LOGI(TAG, "   - All buffers allocated with MALLOC_CAP_DMA flag");

  // Auto-appliquer les gains RGB CCM si configurés dans YAML
  if (this->rgb_gains_enabled_) {
    // Attendre que streaming soit stable (100ms)
    vTaskDelay(pdMS_TO_TICKS(100));

    if (this->set_rgb_gains(this->rgb_gains_red_, this->rgb_gains_green_, this->rgb_gains_blue_)) {
      // ESP_LOGI(TAG, "✓ CCM RGB gains auto-applied: R=%.2f, G=%.2f, B=%.2f",
      //          this->rgb_gains_red_, this->rgb_gains_green_, this->rgb_gains_blue_);
    } else {
      ESP_LOGW(TAG, "⚠️  Failed to auto-apply CCM RGB gains");
    }
  }

  return true;
}

bool MipiDSICamComponent::capture_frame() {
  if (!this->streaming_active_) {
    return false;
  }

  static uint32_t profile_count = 0;
  static uint32_t total_dqbuf_us = 0;
  static uint32_t total_copy_us = 0;
  static uint32_t total_qbuf_us = 0;

  // 1. Dequeue un buffer rempli
  uint32_t t1 = esp_timer_get_time();
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
  uint32_t t2 = esp_timer_get_time();

  // 2. Zero-copy: pointer directement vers le buffer V4L2 RGB565
  this->image_buffer_ = (uint8_t*)this->v4l2_buffers_[buf.index].start;
  uint32_t t3 = esp_timer_get_time();

  this->frame_sequence_++;

  // Log uniquement la première frame
  if (this->frame_sequence_ == 1) {
    ESP_LOGI(TAG, "✅ First frame captured (zero-copy):");
    ESP_LOGI(TAG, "   Buffer size: %u bytes (%ux%u × 2 = RGB565)",
             this->image_buffer_size_, this->image_width_, this->image_height_);
    ESP_LOGI(TAG, "   Buffer address: %p (V4L2 MMAP)", this->image_buffer_);
    ESP_LOGI(TAG, "   Timing: DQBUF=%uus, Pointer assignment=%uus",
             (uint32_t)(t2-t1), (uint32_t)(t3-t2));
    ESP_LOGI(TAG, "   First pixels (RGB565): %02X%02X %02X%02X %02X%02X",
             this->image_buffer_[0], this->image_buffer_[1],
             this->image_buffer_[2], this->image_buffer_[3],
             this->image_buffer_[4], this->image_buffer_[5]);
  }

  // Profiling détaillé toutes les 100 frames
  profile_count++;
  total_dqbuf_us += (t2 - t1);
  total_copy_us += (t3 - t2);  // "copy" = pointer assignment (zero-copy, should be ~0us)

  // 3. Re-queue le buffer immédiatement
  uint32_t t4 = esp_timer_get_time();
  if (ioctl(this->video_fd_, VIDIOC_QBUF, &buf) < 0) {
    ESP_LOGE(TAG, "VIDIOC_QBUF failed: %s", strerror(errno));
    return false;
  }
  uint32_t t5 = esp_timer_get_time();

  total_qbuf_us += (t5 - t4);

  if (profile_count == 100) {
    // Logs de profiling commentés pour réduire verbosité
    // uint32_t avg_dqbuf = total_dqbuf_us / 100;
    // uint32_t avg_pointer = total_copy_us / 100;
    // uint32_t avg_qbuf = total_qbuf_us / 100;
    // uint32_t avg_total = (total_dqbuf_us + total_copy_us + total_qbuf_us) / 100;
    // float fps = 1000000.0f / avg_total;  // Calcul FPS
    //
    // ESP_LOGI(TAG, "📊 Zero-Copy Profiling (avg over 100 frames):");
    // ESP_LOGI(TAG, "   DQBUF: %u us (%.1f ms)", avg_dqbuf, avg_dqbuf / 1000.0f);
    // ESP_LOGI(TAG, "   Pointer assignment: %u us (%.1f ms) ← Zero-copy", avg_pointer, avg_pointer / 1000.0f);
    // ESP_LOGI(TAG, "   QBUF: %u us (%.1f ms)", avg_qbuf, avg_qbuf / 1000.0f);
    // ESP_LOGI(TAG, "   TOTAL: %u us (%.1f ms) → %.1f FPS ← Should be 30+ FPS!",
    //          avg_total, avg_total / 1000.0f, fps);

    profile_count = 0;
    total_dqbuf_us = 0;
    total_copy_us = 0;
    total_qbuf_us = 0;
  }

  return true;
}

void MipiDSICamComponent::stop_streaming() {
  if (!this->streaming_active_) {
    return;
  }

  // ESP_LOGI(TAG, "=== STOP STREAMING ===");

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

  // 3. Reset image_buffer pointer (it pointed to V4L2 buffer, now unmapped)
  this->image_buffer_ = nullptr;

  // 4. Libérer la structure imlib si allouée (seulement si imlib activé)
#if IMLIB_AVAILABLE
  if (this->imlib_image_) {
    free(this->imlib_image_);
    this->imlib_image_ = nullptr;
    this->imlib_image_valid_ = false;
  }
#endif

  // 5. Fermer le device
  if (this->video_fd_ >= 0) {
    close(this->video_fd_);
    this->video_fd_ = -1;
  }

  this->streaming_active_ = false;
  this->image_width_ = 0;
  this->image_height_ = 0;
  this->image_buffer_size_ = 0;

  // ESP_LOGI(TAG, "✓ Streaming stopped, resources freed");
}

// ============================================================================
// Contrôles Manuels d'Exposition et Balance des Blancs
// ============================================================================

/**
 * @brief Définir l'exposition manuelle du capteur
 *
 * Permet de contrôler manuellement l'exposition pour corriger la surexposition.
 * Désactive temporairement l'AEC automatique.
 *
 * @param value Valeur d'exposition (0-65535). Valeurs typiques:
 *              - 1000-5000: Très faible exposition (scènes très lumineuses)
 *              - 5000-15000: Faible exposition (scènes lumineuses)
 *              - 15000-30000: Exposition normale (défaut)
 *              - 30000-50000: Haute exposition (scènes sombres)
 *              - 0: Réactiver AEC automatique
 * @return true si succès, false si erreur
 */
bool MipiDSICamComponent::set_exposure(int value) {
  if (!this->streaming_active_ || this->video_fd_ < 0) {
    ESP_LOGW(TAG, "Cannot set exposure: streaming not active");
    return false;
  }

  // V4L2_CID_EXPOSURE_ABSOLUTE control
  struct v4l2_control ctrl;
  memset(&ctrl, 0, sizeof(ctrl));

  if (value == 0) {
    // Réactiver AEC automatique
    ctrl.id = V4L2_CID_EXPOSURE_AUTO;
    ctrl.value = V4L2_EXPOSURE_AUTO;  // Auto exposure

    if (ioctl(this->video_fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
      ESP_LOGE(TAG, "Failed to enable auto exposure: %s", strerror(errno));
      return false;
    }
    ESP_LOGI(TAG, "✓ Auto exposure enabled (AEC active)");
  } else {
    // Désactiver AEC et définir exposition manuelle
    ctrl.id = V4L2_CID_EXPOSURE_AUTO;
    ctrl.value = V4L2_EXPOSURE_MANUAL;  // Manual exposure

    if (ioctl(this->video_fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
      ESP_LOGW(TAG, "Failed to disable auto exposure: %s", strerror(errno));
      // Continue anyway, try to set exposure value
    }

    // Définir la valeur d'exposition
    ctrl.id = V4L2_CID_EXPOSURE_ABSOLUTE;
    ctrl.value = value;

    if (ioctl(this->video_fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
      ESP_LOGE(TAG, "Failed to set exposure to %d: %s", value, strerror(errno));
      return false;
    }
    ESP_LOGI(TAG, "✓ Manual exposure set to %d (AEC disabled)", value);
  }

  return true;
}

/**
 * @brief Définir le gain manuel du capteur
 *
 * Contrôle le gain analogique/numérique du capteur.
 *
 * @param value Valeur de gain (1000-16000):
 *              - 1000: 1x (gain minimum, image la plus sombre)
 *              - 2000: 2x
 *              - 4000: 4x
 *              - 8000: 8x (défaut recommandé)
 *              - 16000: 16x (gain maximum, image la plus claire mais bruitée)
 * @return true si succès, false si erreur
 */
bool MipiDSICamComponent::set_gain(int value) {
  if (!this->streaming_active_ || this->video_fd_ < 0) {
    ESP_LOGW(TAG, "Cannot set gain: streaming not active");
    return false;
  }

  // V4L2_CID_GAIN control
  struct v4l2_control ctrl;
  memset(&ctrl, 0, sizeof(ctrl));
  ctrl.id = V4L2_CID_GAIN;
  ctrl.value = value;

  if (ioctl(this->video_fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
    ESP_LOGE(TAG, "Failed to set gain to %d: %s", value, strerror(errno));
    return false;
  }

  ESP_LOGI(TAG, "✓ Gain set to %d (%.1fx)", value, value / 1000.0f);
  return true;
}

/**
 * @brief Activer/désactiver la balance des blancs automatique
 *
 * @param auto_mode true pour AWB automatique, false pour manuel
 * @return true si succès, false si erreur
 */
bool MipiDSICamComponent::set_white_balance_mode(bool auto_mode) {
  if (!this->streaming_active_ || this->video_fd_ < 0) {
    ESP_LOGW(TAG, "Cannot set white balance mode: streaming not active");
    return false;
  }

  struct v4l2_control ctrl;
  memset(&ctrl, 0, sizeof(ctrl));
  ctrl.id = V4L2_CID_AUTO_WHITE_BALANCE;
  ctrl.value = auto_mode ? 1 : 0;

  if (ioctl(this->video_fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
    ESP_LOGE(TAG, "Failed to set white balance mode: %s", strerror(errno));
    return false;
  }

  ESP_LOGI(TAG, "✓ White balance: %s", auto_mode ? "AUTO (AWB enabled)" : "MANUAL");
  return true;
}

/**
 * @brief Définir la température de couleur manuelle (balance des blancs)
 *
 * Permet de corriger la dominante de couleur (ex: blanc → vert).
 * Nécessite que AWB soit désactivé (set_white_balance_mode(false)).
 *
 * @param kelvin Température de couleur en Kelvin:
 *               - 2800K: Lampe incandescente (jaune/orange)
 *               - 3200K: Lampe halogène
 *               - 4000K: Fluorescent blanc froid
 *               - 5000K: Lumière du jour (neutre)
 *               - 5500K: Flash électronique (défaut recommandé)
 *               - 6500K: Ciel nuageux (bleuté)
 * @return true si succès, false si erreur
 */
bool MipiDSICamComponent::set_white_balance_temp(int kelvin) {
  if (!this->streaming_active_ || this->video_fd_ < 0) {
    ESP_LOGW(TAG, "Cannot set white balance temperature: streaming not active");
    return false;
  }

  struct v4l2_control ctrl;
  memset(&ctrl, 0, sizeof(ctrl));
  ctrl.id = V4L2_CID_WHITE_BALANCE_TEMPERATURE;
  ctrl.value = kelvin;

  if (ioctl(this->video_fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
    ESP_LOGE(TAG, "Failed to set white balance temperature to %dK: %s", kelvin, strerror(errno));
    return false;
  }

  ESP_LOGI(TAG, "✓ White balance temperature set to %dK", kelvin);
  return true;
}

/**
 * @brief Définir la matrice CCM (Color Correction Matrix) complète 3x3
 *
 * Permet une correction couleur avancée en configurant directement la matrice
 * de correction couleur de l'ISP. Chaque élément peut être dans [-4.0, 4.0].
 *
 * Formule: [R_out, G_out, B_out] = matrix × [R_in, G_in, B_in]
 *
 * @param matrix Matrice 3x3 float (row-major order):
 *               matrix[0][0..2]: Coefficients pour R_out
 *               matrix[1][0..2]: Coefficients pour G_out
 *               matrix[2][0..2]: Coefficients pour B_out
 *
 * Exemple d'identité (aucune correction):
 *   {{1.0, 0.0, 0.0},
 *    {0.0, 1.0, 0.0},
 *    {0.0, 0.0, 1.0}}
 *
 * Exemple correction blanc→vert (M5Stack):
 *   {{1.5, 0.0, 0.0},   // Booster rouge
 *    {0.0, 1.0, 0.0},   // Vert normal
 *    {0.0, 0.0, 1.6}}   // Booster bleu
 *
 * @return true si succès, false si erreur
 */
bool MipiDSICamComponent::set_ccm_matrix(float matrix[3][3]) {
  if (!this->streaming_active_ || this->video_fd_ < 0) {
    ESP_LOGW(TAG, "Cannot set CCM matrix: streaming not active");
    return false;
  }

  // Créer structure CCM avec matrice fournie
  esp_video_isp_ccm_t ccm_config;
  memset(&ccm_config, 0, sizeof(ccm_config));
  ccm_config.enable = true;

  // Copier matrice (dimensions vérifiées par ISP_CCM_DIMENSION)
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      ccm_config.matrix[i][j] = matrix[i][j];
    }
  }

  // Configurer via V4L2 ioctl avec CID personnalisé ESP32
  struct v4l2_ext_control ctrl;
  memset(&ctrl, 0, sizeof(ctrl));
  ctrl.id = V4L2_CID_USER_ESP_ISP_CCM;
  ctrl.ptr = &ccm_config;
  ctrl.size = sizeof(ccm_config);

  struct v4l2_ext_controls ctrls;
  memset(&ctrls, 0, sizeof(ctrls));
  ctrls.count = 1;
  ctrls.controls = &ctrl;

  if (ioctl(this->video_fd_, VIDIOC_S_EXT_CTRLS, &ctrls) < 0) {
    ESP_LOGE(TAG, "Failed to set CCM matrix: %s", strerror(errno));
    return false;
  }

  ESP_LOGI(TAG, "✓ CCM matrix configured:");
  ESP_LOGI(TAG, "  [%.2f, %.2f, %.2f]", matrix[0][0], matrix[0][1], matrix[0][2]);
  ESP_LOGI(TAG, "  [%.2f, %.2f, %.2f]", matrix[1][0], matrix[1][1], matrix[1][2]);
  ESP_LOGI(TAG, "  [%.2f, %.2f, %.2f]", matrix[2][0], matrix[2][1], matrix[2][2]);
  return true;
}

/**
 * @brief Définir les gains RGB de manière simplifiée (matrice diagonale)
 *
 * Wrapper simplifié de set_ccm_matrix() pour ajuster les gains par canal.
 * Crée une matrice CCM diagonale: seuls les gains R, G, B sont modifiés.
 *
 * C'est l'approche utilisée par ESPHome PR#7639 pour corriger blanc→vert.
 *
 * Formule résultante: R_out = R_in × red, G_out = G_in × green, B_out = B_in × blue
 *
 * @param red   Gain canal rouge (ex: 1.3 = +30% rouge)
 * @param green Gain canal vert (ex: 0.85 = -15% vert, corrige blanc→vert)
 * @param blue  Gain canal bleu (ex: 1.25 = +25% bleu)
 *
 * Valeurs typiques pour corriger blanc→vert avec SC202CS:
 *   - Correction légère: (1.2, 0.9, 1.15)
 *   - Correction moyenne: (1.3, 0.85, 1.25) ← RECOMMANDÉ
 *   - Correction M5Stack: (1.5, 1.0, 1.6)
 *
 * @return true si succès, false si erreur
 */
bool MipiDSICamComponent::set_rgb_gains(float red, float green, float blue) {
  // Créer matrice diagonale
  float matrix[3][3] = {
    {red,  0.0f, 0.0f},
    {0.0f, green, 0.0f},
    {0.0f, 0.0f,  blue}
  };

  if (!set_ccm_matrix(matrix)) {
    return false;
  }

  ESP_LOGI(TAG, "✓ RGB gains: R=%.2f, G=%.2f, B=%.2f", red, green, blue);
  return true;
}

/**
 * @brief Définir les gains White Balance de l'ISP (rouge et bleu)
 *
 * Contrôle les gains hardware de white balance de l'ISP (avant CCM).
 * Le gain vert est fixe à 1.0 (référence).
 *
 * Note: Différent de set_rgb_gains() qui modifie la CCM (après demosaic).
 *       L'ordre du pipeline est: Sensor → Demosaic → WB gains → CCM → Output
 *
 * @param red_gain  Gain du canal rouge (typiquement 0.5 - 4.0)
 * @param blue_gain Gain du canal bleu (typiquement 0.5 - 4.0)
 *
 * Valeurs typiques:
 *   - Lumière du jour: red=1.0, blue=1.0 (neutre)
 *   - Incandescent: red=0.7, blue=1.8 (compenser jaune)
 *   - Fluorescent: red=1.3, blue=0.9 (compenser vert)
 *
 * @return true si succès, false si erreur
 */
bool MipiDSICamComponent::set_wb_gains(float red_gain, float blue_gain) {
  if (!this->streaming_active_ || this->video_fd_ < 0) {
    ESP_LOGW(TAG, "Cannot set WB gains: streaming not active");
    return false;
  }

  // Créer structure WB
  esp_video_isp_wb_t wb_config;
  memset(&wb_config, 0, sizeof(wb_config));
  wb_config.enable = true;
  wb_config.red_gain = red_gain;
  wb_config.blue_gain = blue_gain;

  // Configurer via V4L2 ioctl avec CID personnalisé ESP32
  struct v4l2_ext_control ctrl;
  memset(&ctrl, 0, sizeof(ctrl));
  ctrl.id = V4L2_CID_USER_ESP_ISP_WB;
  ctrl.ptr = &wb_config;
  ctrl.size = sizeof(wb_config);

  struct v4l2_ext_controls ctrls;
  memset(&ctrls, 0, sizeof(ctrls));
  ctrls.count = 1;
  ctrls.controls = &ctrl;

  if (ioctl(this->video_fd_, VIDIOC_S_EXT_CTRLS, &ctrls) < 0) {
    ESP_LOGE(TAG, "Failed to set WB gains: %s", strerror(errno));
    return false;
  }

  ESP_LOGI(TAG, "✓ WB gains: Red=%.2f, Blue=%.2f (Green=1.0)", red_gain, blue_gain);
  return true;
}

// ============================================================================
// Contrôles V4L2 Standards (pour ESPHome number components)
// ============================================================================

/**
 * @brief Régler la luminosité de l'image
 * @param value Valeur de luminosité (-128 à 127, défaut: 0)
 */
bool MipiDSICamComponent::set_brightness(int value) {
  if (!this->streaming_active_ || this->video_fd_ < 0) {
    ESP_LOGW(TAG, "Cannot set brightness: streaming not active");
    return false;
  }

  struct v4l2_control ctrl;
  ctrl.id = V4L2_CID_BRIGHTNESS;
  ctrl.value = value;

  if (ioctl(this->video_fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
    ESP_LOGE(TAG, "Failed to set brightness: %s", strerror(errno));
    return false;
  }

  // ESP_LOGI(TAG, "✓ Brightness set to %d", value);
  return true;
}

/**
 * @brief Régler le contraste de l'image
 * @param value Valeur de contraste (0 à 255, défaut: 128)
 */
bool MipiDSICamComponent::set_contrast(int value) {
  if (!this->streaming_active_ || this->video_fd_ < 0) {
    ESP_LOGW(TAG, "Cannot set contrast: streaming not active");
    return false;
  }

  struct v4l2_control ctrl;
  ctrl.id = V4L2_CID_CONTRAST;
  ctrl.value = value;

  if (ioctl(this->video_fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
    ESP_LOGE(TAG, "Failed to set contrast: %s", strerror(errno));
    return false;
  }

  // ESP_LOGI(TAG, "✓ Contrast set to %d", value);
  return true;
}

/**
 * @brief Régler la saturation des couleurs
 * @param value Valeur de saturation (0 à 255, défaut: 128)
 */
bool MipiDSICamComponent::set_saturation(int value) {
  if (!this->streaming_active_ || this->video_fd_ < 0) {
    ESP_LOGW(TAG, "Cannot set saturation: streaming not active");
    return false;
  }

  struct v4l2_control ctrl;
  ctrl.id = V4L2_CID_SATURATION;
  ctrl.value = value;

  if (ioctl(this->video_fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
    ESP_LOGE(TAG, "Failed to set saturation: %s", strerror(errno));
    return false;
  }

  // ESP_LOGI(TAG, "✓ Saturation set to %d", value);
  return true;
}

/**
 * @brief Régler la teinte de l'image
 * @param value Valeur de teinte (-180 à 180, défaut: 0)
 */
bool MipiDSICamComponent::set_hue(int value) {
  if (!this->streaming_active_ || this->video_fd_ < 0) {
    ESP_LOGW(TAG, "Cannot set hue: streaming not active");
    return false;
  }

  struct v4l2_control ctrl;
  ctrl.id = V4L2_CID_HUE;
  ctrl.value = value;

  if (ioctl(this->video_fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
    ESP_LOGE(TAG, "Failed to set hue: %s", strerror(errno));
    return false;
  }

  // ESP_LOGI(TAG, "✓ Hue set to %d", value);
  return true;
}

/**
 * @brief Régler la netteté de l'image (filter)
 * @param value Valeur de netteté (0 à 255, défaut: 128)
 */
bool MipiDSICamComponent::set_sharpness(int value) {
  if (!this->streaming_active_ || this->video_fd_ < 0) {
    ESP_LOGW(TAG, "Cannot set sharpness: streaming not active");
    return false;
  }

  struct v4l2_control ctrl;
  ctrl.id = V4L2_CID_SHARPNESS;
  ctrl.value = value;

  if (ioctl(this->video_fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
    ESP_LOGE(TAG, "Failed to set sharpness: %s", strerror(errno));
    return false;
  }

  // ESP_LOGI(TAG, "✓ Sharpness (filter) set to %d", value);
  return true;
}

// ============================================================================
// imlib - Méthodes de dessin zero-copy sur buffer RGB565
// ============================================================================

#if IMLIB_AVAILABLE

image_t* MipiDSICamComponent::get_imlib_image() {
  if (!this->streaming_active_ || !this->image_buffer_ || this->image_buffer_size_ == 0) {
    ESP_LOGW(TAG, "Cannot get imlib image: no active frame buffer");
    this->imlib_image_valid_ = false;
    return nullptr;
  }

  // Allouer la structure imlib au premier appel
  if (!this->imlib_image_) {
    this->imlib_image_ = (image_t*)malloc(sizeof(image_t));
    if (!this->imlib_image_) {
      ESP_LOGE(TAG, "Failed to allocate imlib image structure");
      return nullptr;
    }
    memset(this->imlib_image_, 0, sizeof(image_t));
  }

  // Initialiser la structure imlib image_t pour pointer vers le buffer V4L2 (zero-copy)
  this->imlib_image_->w = this->image_width_;
  this->imlib_image_->h = this->image_height_;
  this->imlib_image_->pixfmt = PIXFORMAT_RGB565;
  this->imlib_image_->pixels = this->image_buffer_;
  this->imlib_image_valid_ = true;

  return this->imlib_image_;
}

void MipiDSICamComponent::draw_string(int x, int y, const char *text, uint16_t color, float scale) {
  image_t *img = this->get_imlib_image();
  if (!img) return;

  imlib_draw_string(img, x, y, text, color, scale, 1, 1, 0, false, false, PIXFORMAT_RGB565, nullptr);
}

void MipiDSICamComponent::draw_line(int x0, int y0, int x1, int y1, uint16_t color, int thickness) {
  image_t *img = this->get_imlib_image();
  if (!img) return;

  imlib_draw_line(img, x0, y0, x1, y1, color, thickness);
}

void MipiDSICamComponent::draw_rectangle(int x, int y, int w, int h, uint16_t color, int thickness, bool fill) {
  image_t *img = this->get_imlib_image();
  if (!img) return;

  imlib_draw_rectangle(img, x, y, w, h, color, thickness, fill);
}

void MipiDSICamComponent::draw_circle(int cx, int cy, int radius, uint16_t color, int thickness, bool fill) {
  image_t *img = this->get_imlib_image();
  if (!img) return;

  imlib_draw_circle(img, cx, cy, radius, color, thickness, fill);
}

int MipiDSICamComponent::get_pixel(int x, int y) {
  image_t *img = this->get_imlib_image();
  if (!img) return 0;

  return imlib_get_pixel(img, x, y);
}

void MipiDSICamComponent::set_pixel(int x, int y, uint16_t color) {
  image_t *img = this->get_imlib_image();
  if (!img) return;

  imlib_set_pixel(img, x, y, color);
}

#else  // IMLIB_AVAILABLE == 0

// Stubs imlib (imlib désactivé) - retournent sans erreur
image_t* MipiDSICamComponent::get_imlib_image() {
  ESP_LOGW(TAG, "imlib drawing disabled (compile with -DENABLE_IMLIB_DRAWING to enable)");
  return nullptr;
}

void MipiDSICamComponent::draw_string(int x, int y, const char *text, uint16_t color, float scale) {
  // Stub - ne fait rien
}

void MipiDSICamComponent::draw_line(int x0, int y0, int x1, int y1, uint16_t color, int thickness) {
  // Stub - ne fait rien
}

void MipiDSICamComponent::draw_rectangle(int x, int y, int w, int h, uint16_t color, int thickness, bool fill) {
  // Stub - ne fait rien
}

void MipiDSICamComponent::draw_circle(int cx, int cy, int radius, uint16_t color, int thickness, bool fill) {
  // Stub - ne fait rien
}

int MipiDSICamComponent::get_pixel(int x, int y) {
  return 0;  // Stub - retourne noir
}

void MipiDSICamComponent::set_pixel(int x, int y, uint16_t color) {
  // Stub - ne fait rien
}

#endif  // IMLIB_AVAILABLE

}  // namespace mipi_dsi_cam
}  // namespace esphome




