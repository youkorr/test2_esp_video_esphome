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

  this->update_interval_ = 33;  // Intervalle théorique (~30 FPS)
  this->last_update_ = 0;
  this->frame_count_ = 0;
  this->first_update_ = true;

  ESP_LOGI(TAG, "✅ LVGL Camera Display initialisé");
  ESP_LOGI(TAG, "   Intervalle cible: %u ms (~%d FPS)",
           this->update_interval_, 1000 / this->update_interval_);
}

void LVGLCameraDisplay::loop() {
  if (!this->camera_->is_streaming())
    return;

  // Capturer une frame dès qu’elle est prête
  if (this->camera_->capture_frame()) {
    this->update_canvas_();
    this->frame_count_++;

    // Log FPS réel toutes les 3 secondes
    uint32_t now = millis();
    if (now - this->last_update_ >= 3000) {
      float fps = (float)this->frame_count_ / ((now - this->last_update_) / 1000.0f);
      ESP_LOGI(TAG, "🎞️ %u frames affichées - FPS moyen: %.2f", this->frame_count_, fps);
      this->frame_count_ = 0;
      this->last_update_ = now;
    }
  }
}

void LVGLCameraDisplay::dump_config() {
  ESP_LOGCONFIG(TAG, "LVGL Camera Display:");
  ESP_LOGCONFIG(TAG, "  Intervalle cible: %u ms (~%d FPS)", this->update_interval_, 1000 / this->update_interval_);
  ESP_LOGCONFIG(TAG, "  Canvas configuré: %s", this->canvas_obj_ ? "OUI" : "NON");
}

void LVGLCameraDisplay::update_canvas_() {
  if (this->camera_ == nullptr || this->canvas_obj_ == nullptr)
    return;

  uint8_t *img_data = this->camera_->get_image_data();
  uint16_t width = this->camera_->get_image_width();
  uint16_t height = this->camera_->get_image_height();

  if (img_data == nullptr)
    return;

  // Premier affichage → informations de debug
  if (this->first_update_) {
    ESP_LOGI(TAG, "🖼️ Premier update canvas (mode direct sans copie):");
    ESP_LOGI(TAG, "   Dimensions: %ux%u", width, height);
    ESP_LOGI(TAG, "   Buffer caméra: %p", img_data);
    ESP_LOGI(TAG, "   Premiers pixels (RGB565): %02X%02X %02X%02X %02X%02X",
             img_data[0], img_data[1], img_data[2], img_data[3], img_data[4], img_data[5]);
    this->first_update_ = false;

    // Configure LVGL une seule fois avec le buffer caméra
    lv_canvas_set_buffer(this->canvas_obj_, img_data, width, height, LV_IMG_CF_TRUE_COLOR);
  } else {
    // Met simplement à jour le pointeur du buffer LVGL (nouveau frame)
    lv_canvas_set_buffer(this->canvas_obj_, img_data, width, height, LV_IMG_CF_TRUE_COLOR);
  }

  // Rafraîchit le canvas à l’écran
  lv_obj_invalidate(this->canvas_obj_);
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


