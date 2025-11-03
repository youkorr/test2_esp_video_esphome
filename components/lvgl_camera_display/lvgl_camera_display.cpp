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
  this->lv_buffer_ = nullptr;

  ESP_LOGI(TAG, "✅ LVGL Camera Display initialisé");
  ESP_LOGI(TAG, "   Intervalle cible: %u ms (~%d FPS)",
           this->update_interval_, 1000 / this->update_interval_);
}

void LVGLCameraDisplay::loop() {
  if (!this->camera_->is_streaming())
    return;

  // Capturer une frame dès qu’elle est prête (pas d’attente fixe)
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
    ESP_LOGI(TAG, "🖼️ Premier update canvas:");
    ESP_LOGI(TAG, "   Dimensions: %ux%u", width, height);
    ESP_LOGI(TAG, "   Buffer caméra: %p", img_data);
    ESP_LOGI(TAG, "   Premiers pixels (RGB565): %02X%02X %02X%02X %02X%02X",
             img_data[0], img_data[1], img_data[2], img_data[3], img_data[4], img_data[5]);
    this->first_update_ = false;
  }

  // Taille totale du frame (en octets)
  size_t buf_size = width * height * 2;

  // Alloue un buffer LVGL interne (RAM interne si possible, sinon SPIRAM)
  if (this->lv_buffer_ == nullptr) {
    this->lv_buffer_ = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL);
    if (this->lv_buffer_ == nullptr) {
      // Fallback automatique
      this->lv_buffer_ = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    }

    if (this->lv_buffer_ == nullptr) {
      ESP_LOGE(TAG, "❌ Impossible d'allouer le buffer LVGL (%u octets)", buf_size);
      return;
    }

    ESP_LOGI(TAG, "🧠 Buffer LVGL alloué (%u octets, %s)",
             buf_size,
             heap_caps_check_integrity_all(true) ? "intégrité OK" : "vérifié");
    lv_canvas_set_buffer(this->canvas_obj_, this->lv_buffer_, width, height, LV_IMG_CF_TRUE_COLOR);
  }

  // Copie rapide du frame caméra vers le buffer LVGL
  memcpy(this->lv_buffer_, img_data, buf_size);

  // Demande à LVGL de redessiner le canvas
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


