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

  ESP_LOGI(TAG, "✅ LVGL Camera Display initialisé");
  ESP_LOGI(TAG, "   Camera: Opérationnelle");
  ESP_LOGI(TAG, "   Mode: Tâche FreeRTOS dédiée (comme M5Stack demo)");
  ESP_LOGI(TAG, "   Canvas sera configuré lors de l'appel configure_canvas()");
}

void LVGLCameraDisplay::loop() {
  // La CAPTURE est gérée par camera_task dans mipi_dsi_cam,
  // mais la MISE À JOUR DU CANVAS doit se faire ici (contexte LVGL)
  // pour éviter le warning "modifying dirty areas in render"

  if (this->camera_ != nullptr) {
    this->camera_->update_canvas_if_ready();
  }
}

void LVGLCameraDisplay::dump_config() {
  ESP_LOGCONFIG(TAG, "LVGL Camera Display:");
  ESP_LOGCONFIG(TAG, "  Mode: Tâche FreeRTOS dédiée");
  ESP_LOGCONFIG(TAG, "  FPS attendu: 25-30 FPS");
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

  uint8_t* img_data = this->camera_->get_image_data();
  uint16_t width = this->camera_->get_image_width();
  uint16_t height = this->camera_->get_image_height();

  if (img_data == nullptr) {
    return;
  }

  if (this->first_update_) {
    ESP_LOGI(TAG, "🖼️  Premier update canvas:");
    ESP_LOGI(TAG, "   Dimensions: %ux%u", width, height);
    ESP_LOGI(TAG, "   Buffer: %p", img_data);
    ESP_LOGI(TAG, "   Premiers pixels (RGB565): %02X%02X %02X%02X %02X%02X", 
             img_data[0], img_data[1], img_data[2], img_data[3], img_data[4], img_data[5]);
    this->first_update_ = false;
  }

  // Mettre à jour le buffer du canvas (COMME LA DÉMO M5STACK)
  // NOTE: lv_canvas_set_buffer ne copie PAS les données - il pointe juste vers le buffer
  // IMPORTANT: Ne PAS appeler lv_obj_invalidate() - c'est ce qui cause le FPS faible!
  // LVGL redessine automatiquement lors de son prochain cycle de rafraîchissement

  lv_canvas_set_buffer(this->canvas_obj_, img_data, width, height, LV_IMG_CF_TRUE_COLOR);

  // PAS de lv_obj_invalidate() ici! La démo M5Stack ne l'utilise pas.
  // lv_obj_invalidate() force un redraw complet de 1.8MB, ce qui prend ~250ms
  // et limite le FPS à 3-4 au lieu de 30.
}

void LVGLCameraDisplay::configure_canvas(lv_obj_t *canvas) {
  this->canvas_obj_ = canvas;
  ESP_LOGI(TAG, "🎨 Canvas configuré: %p", canvas);

  if (canvas != nullptr) {
    lv_coord_t w = lv_obj_get_width(canvas);
    lv_coord_t h = lv_obj_get_height(canvas);
    ESP_LOGI(TAG, "   Taille canvas: %dx%d", w, h);

    // Démarrer la tâche FreeRTOS dédiée pour capture haute performance
    if (this->camera_ != nullptr) {
      ESP_LOGI(TAG, "🚀 Démarrage tâche FreeRTOS caméra...");
      if (this->camera_->start_camera_task(canvas)) {
        ESP_LOGI(TAG, "   ✅ Tâche démarrée - FPS optimal attendu (25-30 FPS)");
      } else {
        ESP_LOGE(TAG, "   ❌ Échec démarrage tâche caméra");
      }
    }
  }
}

}  // namespace lvgl_camera_display
}  // namespace esphome







