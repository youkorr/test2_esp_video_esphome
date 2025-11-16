#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include <string>
#include <vector>

// Forward declaration pour imlib (défini dans .cpp pour éviter dépendance header)
struct image;
typedef struct image image_t;

// Définition du type ISP config basée sur le code source ESP-Video
#ifndef ESP_VIDEO_ISP_CONFIG_DEFINED
#define ESP_VIDEO_ISP_CONFIG_DEFINED

typedef struct {
    const char *isp_dev;
    const char *cam_dev;
    void *ipa_config;
} esp_video_isp_config_t;

#endif

namespace esphome {
namespace mipi_dsi_cam {

// Simple buffer element pour triple buffering (remplace esp_video_buffer)
struct SimpleBufferElement {
  uint8_t *data;      // Pointeur vers données RGB565
  bool allocated;     // true = en cours d'utilisation
  uint32_t index;     // Index du buffer (0, 1, 2)
};

class MipiDSICamComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void dump_config() override;

  void set_sensor_type(const std::string &s) { sensor_name_ = s; }
  void set_i2c_id(int id) { i2c_id_ = id; i2c_bus_name_.clear(); }
  void set_i2c_id(const std::string &bus_name) {
    i2c_bus_name_ = bus_name;
    char *end;
    long val = strtol(bus_name.c_str(), &end, 10);
    if (end != bus_name.c_str() && *end == '\0') {
      i2c_id_ = (int)val;
    } else {
      i2c_id_ = 0;
    }
  }
  void set_lane(int l) { lane_ = l; }
  void set_xclk_pin(const std::string &p) { xclk_pin_ = p; }
  void set_xclk_freq(int f) { xclk_freq_ = f; }
  void set_sensor_addr(int a) { sensor_addr_ = a; }
  void set_resolution(const std::string &r) { resolution_ = r; }
  void set_pixel_format(const std::string &f) { pixel_format_ = f; }
  void set_framerate(int f) { framerate_ = f; }
  void set_jpeg_quality(int q) { jpeg_quality_ = q; }

  // Configuration mirror/rotate (PPA hardware si disponible)
  void set_mirror_x(bool enable) { mirror_x_ = enable; }
  void set_mirror_y(bool enable) { mirror_y_ = enable; }
  void set_rotation(int degrees) { rotation_ = degrees; }  // 0, 90, 180, 270
  void set_crop_offset_x(int offset) { crop_offset_x_ = offset; }  // PPA crop offset (pixels)

  // Configuration des gains RGB CCM depuis YAML
  void set_rgb_gains_config(float red, float green, float blue) {
    rgb_gains_red_ = red;
    rgb_gains_green_ = green;
    rgb_gains_blue_ = blue;
    rgb_gains_enabled_ = true;
  }

  // Configuration des contrôles caméra V4L2 depuis YAML
  void add_camera_control(const std::string &control_id, int initial_value) {
    camera_controls_.push_back({control_id, initial_value});
  }

  bool capture_snapshot_to_file(const std::string &path);
  bool is_pipeline_ready() const { return pipeline_started_; }

  // API pour lvgl_camera_display (streaming continu avec buffer pool)
  bool is_streaming() const { return streaming_active_; }
  bool start_streaming();
  void stop_streaming();
  bool capture_frame();

  // Buffer pool APIs (thread-safe, zero-tearing)
  SimpleBufferElement* acquire_buffer();  // Acquiert buffer pour affichage (doit être libéré)
  void release_buffer(SimpleBufferElement *element);  // Libère buffer après affichage

  // Helper functions pour accéder aux buffer elements
  uint8_t* get_buffer_data(SimpleBufferElement *element);  // Retourne pointeur vers données
  uint32_t get_buffer_index(SimpleBufferElement *element);  // Retourne index du buffer

  /**
   * @brief Get current RGB565 frame for face detection or image processing
   * @param[out] buffer_out Pointer to acquired buffer element (must be released with release_buffer())
   * @param[out] data Pointer to RGB565 data
   * @param[out] width Frame width in pixels
   * @param[out] height Frame height in pixels
   * @return true if frame available, false if not streaming or no buffer available
   *
   * @note IMPORTANT: Caller MUST call release_buffer(buffer_out) when done processing!
   *
   * Example usage:
   * @code
   * SimpleBufferElement* buffer = nullptr;
   * uint8_t* data = nullptr;
   * int width, height;
   * if (camera->get_current_rgb_frame(&buffer, &data, &width, &height)) {
   *   // Process frame data (RGB565 format)
   *   // ...
   *   camera->release_buffer(buffer);  // ← REQUIRED!
   * }
   * @endcode
   */
  bool get_current_rgb_frame(SimpleBufferElement **buffer_out, uint8_t **data, int *width, int *height);

  // Legacy API (deprecated, utiliser acquire_buffer/release_buffer)
  uint8_t* get_image_data() { return image_buffer_; }
  uint16_t get_image_width() const { return image_width_; }
  uint16_t get_image_height() const { return image_height_; }
  size_t get_image_size() const { return image_buffer_size_; }

  // Contrôles manuels d'exposition et couleur (pour corriger surexposition et blanc→vert)
  bool set_exposure(int value);     // Contrôle manuel de l'exposition (0-65535, défaut: auto)
  bool set_gain(int value);          // Contrôle manuel du gain (1000-16000, 1000=1x, 16000=16x)
  bool set_white_balance_mode(bool auto_mode);  // true=auto AWB, false=manuel
  bool set_white_balance_temp(int kelvin);      // Température couleur (2800-6500K)

  // Contrôles ISP avancés (correction couleur précise via CCM et WB)
  bool set_ccm_matrix(float matrix[3][3]);  // Matrice CCM 3x3 complète (correction couleur avancée)
  bool set_rgb_gains(float red, float green, float blue);  // Gains RGB diagonaux simplifiés (corrige blanc→vert)
  bool set_wb_gains(float red_gain, float blue_gain);      // White balance ISP (gains R/B, G=1.0)

  // Contrôles V4L2 standards (pour ESPHome number components)
  bool set_brightness(int value);    // -128 à 127, défaut: 0
  bool set_contrast(int value);      // 0 à 255, défaut: 128
  bool set_saturation(int value);    // 0 à 255, défaut: 128
  bool set_hue(int value);           // -180 à 180, défaut: 0
  bool set_sharpness(int value);     // 0 à 255 (filter/sharpness control)

  // imlib - Dessin zero-copy sur buffer RGB565 (améliore fluidité)
  image_t* get_imlib_image();  // Retourne image_t wrappant le buffer caméra actuel
  void draw_string(int x, int y, const char *text, uint16_t color, float scale = 1.0f);
  void draw_line(int x0, int y0, int x1, int y1, uint16_t color, int thickness = 1);
  void draw_rectangle(int x, int y, int w, int h, uint16_t color, int thickness = 1, bool fill = false);
  void draw_circle(int cx, int cy, int radius, uint16_t color, int thickness = 1, bool fill = false);
  int get_pixel(int x, int y);  // Lire pixel RGB565 à (x,y)
  void set_pixel(int x, int y, uint16_t color);  // Écrire pixel RGB565 à (x,y)

 protected:
  std::string sensor_name_{"sc202cs"};
  int i2c_id_{0};
  std::string i2c_bus_name_;
  int lane_{1};
  std::string xclk_pin_{"GPIO36"};
  int xclk_freq_{24000000};
  int sensor_addr_{0x36};
  std::string resolution_{"720P"};
  std::string pixel_format_{"JPEG"};
  int framerate_{30};
  int jpeg_quality_{10};

  // Configuration mirror/rotate (M5Stack-style PPA hardware)
  bool mirror_x_{false};
  bool mirror_y_{false};
  int rotation_{0};  // 0, 90, 180, 270 degrees
  int crop_offset_x_{0};  // PPA crop offset in pixels (from left)

  // PPA (Pixel-Processing Accelerator) hardware handles
  void *ppa_client_handle_{nullptr};
  bool ppa_enabled_{false};

  // Configuration CCM RGB gains depuis YAML
  bool rgb_gains_enabled_{false};
  float rgb_gains_red_{1.0f};
  float rgb_gains_green_{1.0f};
  float rgb_gains_blue_{1.0f};

  // Configuration des contrôles caméra V4L2 depuis YAML
  struct CameraControl {
    std::string id;
    int value;
  };
  std::vector<CameraControl> camera_controls_;

  // Tous en pointeurs void* pour éviter les types incomplets
  void *sensor_dev_{nullptr};
  void *init_cfg_{nullptr};
  esp_video_isp_config_t isp_cfg_{};
  bool pipeline_started_{false};

  uint32_t last_health_check_{0};
  uint32_t snapshot_count_{0};
  uint32_t error_count_{0};

  // État du streaming vidéo continu
  bool streaming_active_{false};
  int video_fd_{-1};       // /dev/video0 (CSI) pour capture frames
  int isp_fd_{-1};         // /dev/video20 (ISP) pour contrôles V4L2 (brightness, contrast, etc.)

  // Buffer pool system (V4L2_MEMORY_USERPTR - zero-copy to SPIRAM)
  SimpleBufferElement simple_buffers_[3];  // Triple buffering
  int current_buffer_index_{-1};  // Index du buffer actuellement capturé (-1 = aucun)
  portMUX_TYPE buffer_mutex_;  // Spinlock pour thread-safety (initialisé dans setup)

  // Legacy pointer (deprecated, pointe vers current_buffer_ si disponible)
  uint8_t *image_buffer_{nullptr};
  size_t image_buffer_size_{0};
  uint16_t image_width_{0};
  uint16_t image_height_{0};
  uint32_t frame_sequence_{0};

  // imlib image wrapper (zero-copy, pointe vers image_buffer_)
  image_t *imlib_image_{nullptr};  // Pointeur vers structure imlib (allouée dans .cpp)
  bool imlib_image_valid_{false};

  bool check_pipeline_health_();
  void cleanup_pipeline_();
  bool apply_v4l2_control_(const std::string &control_id, int value);

  // PPA (Pixel-Processing Accelerator) hardware transform functions
  bool init_ppa_();
  bool apply_ppa_transform_(uint8_t *src_buffer, uint8_t *dst_buffer);
  void cleanup_ppa_();
};

using MipiDsiCam = MipiDSICamComponent;

template<typename... Ts>
class CaptureSnapshotAction : public Action<Ts...>, public Parented<MipiDSICamComponent> {
 public:
  TEMPLATABLE_VALUE(std::string, filename)

  void play(Ts... x) override {
    auto filename = this->filename_.value(x...);
    if (!this->parent_->capture_snapshot_to_file(filename)) {
      ESP_LOGE("mipi_dsi_cam", "Échec de la capture snapshot vers: %s", filename.c_str());
    }
  }
};

template<typename... Ts>
class StartStreamingAction : public Action<Ts...>, public Parented<MipiDSICamComponent> {
 public:
  void play(Ts... x) override {
    if (this->parent_->start_streaming()) {
      ESP_LOGI("mipi_dsi_cam", "✅ Streaming vidéo démarré");
    } else {
      ESP_LOGE("mipi_dsi_cam", "❌ Échec du démarrage du streaming");
    }
  }
};

template<typename... Ts>
class StopStreamingAction : public Action<Ts...>, public Parented<MipiDSICamComponent> {
 public:
  void play(Ts... x) override {
    this->parent_->stop_streaming();
    ESP_LOGI("mipi_dsi_cam", "⏹️  Streaming vidéo arrêté");
  }
};

}  // namespace mipi_dsi_cam
}  // namespace esphome












