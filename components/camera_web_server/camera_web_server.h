#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/mipi_dsi_cam/mipi_dsi_cam.h"

#ifdef USE_ESP_IDF
#include <esp_http_server.h>
#include "driver/jpeg_encode.h"
#endif

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

 protected:
  mipi_dsi_cam::MipiDSICamComponent *camera_{nullptr};
  uint16_t port_{8080};
  bool enable_stream_{true};
  bool enable_snapshot_{true};

#ifdef USE_ESP_IDF
  httpd_handle_t server_{nullptr};

  // JPEG encoder hardware
  jpeg_encoder_handle_t jpeg_handle_{nullptr};
  uint8_t *jpeg_buffer_{nullptr};
  size_t jpeg_buffer_size_{0};
  int jpeg_quality_{80};  // 0-100

  esp_err_t start_server_();
  void stop_server_();
  esp_err_t init_jpeg_encoder_();
  void cleanup_jpeg_encoder_();

  // HTTP handlers (static pour compatibilité avec httpd API C)
  static esp_err_t stream_handler_(httpd_req_t *req);
  static esp_err_t snapshot_handler_(httpd_req_t *req);
  static esp_err_t status_handler_(httpd_req_t *req);
#endif
};

}  // namespace camera_web_server
}  // namespace esphome
