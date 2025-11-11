#include "lvgl_camera_display.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace lvgl_camera_display {

static const char *const TAG = "lvgl_camera_display";

void LVGLCameraDisplay::setup() {
  ESP_LOGCONFIG(TAG, "🎥 Configuration LVGL Camera Display...");

  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "❌ Camera non configurée");
    this->mark_failed();
    return;
  }

  // Vérifier que la caméra est opérationnelle
  if (!this->camera_->is_pipeline_ready()) {
    ESP_LOGE(TAG, "❌ Camera non opérationnelle - pipeline non démarré");
    ESP_LOGE(TAG, "   Le composant mipi_dsi_cam a échoué à s'initialiser");
    ESP_LOGE(TAG, "   Vérifiez les logs de mipi_dsi_cam pour plus de détails");
    this->mark_failed();
    return;
  }

  // Créer un timer LVGL pour les mises à jour de la caméra
  // Ceci garantit des updates réguliers même si ESPHome loop() est lent
  this->lvgl_timer_ = lv_timer_create(lvgl_timer_callback_, this->update_interval_, this);
  if (this->lvgl_timer_ == nullptr) {
    ESP_LOGE(TAG, "❌ Échec création du timer LVGL");
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "✅ LVGL Camera Display initialisé");
  ESP_LOGI(TAG, "   Camera: Opérationnelle");
  ESP_LOGI(TAG, "   Update interval: %u ms (~%d FPS) via LVGL timer",
           this->update_interval_, 1000 / this->update_interval_);
}

void LVGLCameraDisplay::loop() {
  // Loop() est maintenant vide car nous utilisons un timer LVGL
  // Le timer LVGL appelle lvgl_timer_callback_() à intervalle régulier
  // Ceci est plus fiable que de dépendre de la fréquence de loop()
}

// Callback du timer LVGL (appelé périodiquement par LVGL)
void LVGLCameraDisplay::lvgl_timer_callback_(lv_timer_t *timer) {
  LVGLCameraDisplay *display = static_cast<LVGLCameraDisplay *>(timer->user_data);
  if (display != nullptr) {
    display->update_camera_frame_();
  }
}

// Mise à jour de la frame caméra (appelée par le timer LVGL)
void LVGLCameraDisplay::update_camera_frame_() {
  // Si la caméra est en streaming, capturer ET mettre à jour le canvas
  if (!this->camera_->is_streaming()) {
    return;
  }

  // Statistiques de frames manquées
  static uint32_t attempts = 0;
  static uint32_t skipped = 0;

  uint32_t t1 = millis();
  bool frame_captured = this->camera_->capture_frame();
  uint32_t t2 = millis();

  attempts++;
  if (!frame_captured) {
    skipped++;
    return;
  }

  this->update_canvas_();
  uint32_t t3 = millis();
  this->frame_count_++;

  // Accumuler les temps pour statistiques
  static uint32_t last_time = 0;
  static uint32_t total_capture_ms = 0;
  static uint32_t total_canvas_ms = 0;

  total_capture_ms += (t2 - t1);
  total_canvas_ms += (t3 - t2);

  // Logger performance toutes les 100 frames
  if (this->frame_count_ % 100 == 0) {
    uint32_t now_time = millis();

    if (last_time > 0) {
      float elapsed = (now_time - last_time) / 1000.0f;  // secondes
      float fps = 100.0f / elapsed;
      float avg_capture = total_capture_ms / 100.0f;
      float avg_canvas = total_canvas_ms / 100.0f;
      float skip_rate = (skipped * 100.0f) / attempts;
      ESP_LOGI(TAG, "🎞️ %u frames - FPS: %.2f | capture: %.1fms | canvas: %.1fms | skip: %.1f%%",
               this->frame_count_, fps, avg_capture, avg_canvas, skip_rate);
    }
    last_time = now_time;
    total_capture_ms = 0;
    total_canvas_ms = 0;
    attempts = 0;
    skipped = 0;
  }
}

void LVGLCameraDisplay::dump_config() {
  ESP_LOGCONFIG(TAG, "LVGL Camera Display:");
  ESP_LOGCONFIG(TAG, "  Update interval: %u ms", this->update_interval_);
  ESP_LOGCONFIG(TAG, "  FPS cible: ~%d", 1000 / this->update_interval_);
  ESP_LOGCONFIG(TAG, "  Canvas configuré: %s", this->canvas_obj_ ? "OUI" : "NON");
}

void LVGLCameraDisplay::update_canvas_() {
  if (this->camera_ == nullptr) {
    return;
  }

  if (this->canvas_obj_ == nullptr) {
    if (!this->canvas_warning_shown_) {
      ESP_LOGW(TAG, "❌ Canvas null - pas encore configuré?");
      this->canvas_warning_shown_ = true;
    }
    return;
  }

  // Libérer l'ancien buffer affiché (si présent)
  if (this->displayed_buffer_ != nullptr) {
    this->camera_->release_buffer(this->displayed_buffer_);
    this->displayed_buffer_ = nullptr;
  }

  // Acquérir le nouveau buffer depuis le pool
  struct esp_video_buffer_element *buffer = this->camera_->acquire_buffer();
  if (buffer == nullptr) {
    // Pas de buffer disponible - garder l'affichage précédent
    return;
  }

  uint8_t* img_data = this->camera_->get_buffer_data(buffer);
  uint16_t width = this->camera_->get_image_width();
  uint16_t height = this->camera_->get_image_height();

  if (img_data == nullptr) {
    return;
  }

  if (this->first_update_) {
    ESP_LOGI(TAG, "🖼️  Premier update canvas (buffer pool):");
    ESP_LOGI(TAG, "   Dimensions: %ux%u", width, height);
    ESP_LOGI(TAG, "   Buffer: %p (index=%u)", img_data, this->camera_->get_buffer_index(buffer));
    ESP_LOGI(TAG, "   Premiers pixels (RGB565): %02X%02X %02X%02X %02X%02X",
             img_data[0], img_data[1], img_data[2], img_data[3], img_data[4], img_data[5]);
    this->first_update_ = false;
  }

  lv_canvas_set_buffer(this->canvas_obj_, img_data, width, height, LV_IMG_CF_TRUE_COLOR);
  lv_obj_invalidate(this->canvas_obj_);

  // Tracker ce buffer pour le libérer au prochain update
  this->displayed_buffer_ = buffer;
}

void LVGLCameraDisplay::configure_canvas(lv_obj_t *canvas) { 
  this->canvas_obj_ = canvas;
  ESP_LOGI(TAG, "🎨 Canvas configuré: %p", canvas);

  if (canvas != nullptr) {
    lv_coord_t w = lv_obj_get_width(canvas);
    lv_coord_t h = lv_obj_get_height(canvas);
    ESP_LOGI(TAG, "   Taille canvas: %dx%d", w, h);
  }
}

}  // namespace lvgl_camera_display
}  // namespace esphome







