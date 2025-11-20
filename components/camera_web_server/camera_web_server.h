#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/mipi_dsi_cam/mipi_dsi_cam.h"

#ifdef USE_ESP_IDF
#include <esp_http_server.h>

// Tous les headers C d'esp-video doivent être protégés via extern "C"
extern "C" {
#include <sys/types.h>
#include <sys/time.h>

#include "esp_video_init.h"
#include "esp_video_device.h"
#include "esp_video_ioctl.h"
#include "esp_video_isp_ioctl.h"
#include "esp_ipa.h"
#include "esp_ipa_types.h"
#include "driver/ppa.h"          // Pixel-Processing Accelerator hardware
#include "linux/videodev2.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}
#endif  // USE_ESP_IDF

namespace esphome {
namespace camera_web_server {

class CameraWebServer : public Component {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_camera(mipi_dsi_cam::MipiDSICamComponent *camera) { camera_ = camera; }
  void set_port(uint16_t port) { port_ = port; }
  void set_enable_stream(bool enable) { enable_stream_ = enable; }
  void set_enable_snapshot(bool enable) { enable_snapshot_ = enable; }
  void set_enabled(bool enabled) { enabled_ = enabled; }

 protected:
  // --- CAMERA ---
  mipi_dsi_cam::MipiDSICamComponent *camera_{nullptr};

  // --- CONFIG WEBSERVER ---
  uint16_t port_{8080};
  bool enable_stream_{true};
  bool enable_snapshot_{true};
  bool enabled_{false};  // Activé/désactivé via switch HA

#ifdef USE_ESP_IDF
  // --- HTTP SERVER ---
  httpd_handle_t server_{nullptr};

  // Démarrage / arrêt du serveur HTTP
  esp_err_t start_server_();
  void stop_server_();

  // Initialisation et cleanup du pipeline JPEG M2M (/dev/video10, V4L2)
  // NOTE : lazy-init, appelée seulement depuis /pic et /stream quand la
  // caméra a déjà une résolution valide (≠ 0x0).
  esp_err_t init_jpeg_encoder_();
  void cleanup_jpeg_encoder_();

  // --- HANDLERS HTTP (statiques pour httpd API C) ---
  static esp_err_t stream_handler_(httpd_req_t *req);
  static esp_err_t snapshot_handler_(httpd_req_t *req);
  static esp_err_t status_handler_(httpd_req_t *req);
  static esp_err_t info_handler_(httpd_req_t *req);
  static esp_err_t view_handler_(httpd_req_t *req);  // <-- pour /view (image + FPS)
#endif  // USE_ESP_IDF
};

}  // namespace camera_web_server
}  // namespace esphome





