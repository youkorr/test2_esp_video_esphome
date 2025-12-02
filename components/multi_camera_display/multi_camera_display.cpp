#include "multi_camera_display.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace multi_camera_display {

static const char *const TAG = "multi_camera_display";

void MultiCameraDisplay::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Multi Camera Display...");
  ESP_LOGI(TAG, "  Number of cameras: %d", this->cameras_.size());

  if (this->cameras_.size() > 4) {
    ESP_LOGW(TAG, "More than 4 cameras configured, only first 4 will be shown");
  }

  ESP_LOGI(TAG, "Multi Camera Display initialized");
}

void MultiCameraDisplay::loop() {
  // Nothing to do in loop, LVGL handles the display
}

void MultiCameraDisplay::dump_config() {
  ESP_LOGCONFIG(TAG, "Multi Camera Display:");
  ESP_LOGCONFIG(TAG, "  Number of cameras: %d", this->cameras_.size());
}

void MultiCameraDisplay::configure_canvas(lv_obj_t *canvas) {
  this->canvas_obj_ = canvas;
  ESP_LOGI(TAG, "Canvas configured: %p", canvas);

  if (canvas != nullptr) {
    lv_coord_t w = lv_obj_get_width(canvas);
    lv_coord_t h = lv_obj_get_height(canvas);
    ESP_LOGI(TAG, "  Canvas size: %dx%d", w, h);

    // Setup layouts
    this->setup_grid_layout_();
    this->setup_fullscreen_layout_();

    // Show grid by default
    this->show_grid_();
  }
}

void MultiCameraDisplay::setup_grid_layout_() {
  if (this->canvas_obj_ == nullptr) {
    return;
  }

  // Create container for grid
  this->grid_container_ = lv_obj_create(this->canvas_obj_);
  lv_obj_set_size(this->grid_container_, lv_pct(100), lv_pct(100));
  lv_obj_set_pos(this->grid_container_, 0, 0);
  lv_obj_set_style_bg_opa(this->grid_container_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(this->grid_container_, 0, 0);
  lv_obj_set_style_pad_all(this->grid_container_, 5, 0);

  // Grid layout: 2x2
  lv_obj_set_layout(this->grid_container_, LV_LAYOUT_GRID);

  static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(this->grid_container_, col_dsc, row_dsc);

  // Create thumbnail buttons for each camera
  int num_cameras = std::min((int)this->cameras_.size(), 4);
  for (int i = 0; i < num_cameras; i++) {
    // Create button container
    lv_obj_t *btn = lv_btn_create(this->grid_container_);
    lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, i % 2, 1,
                         LV_GRID_ALIGN_STRETCH, i / 2, 1);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(btn, 2, 0);
    lv_obj_set_style_radius(btn, 8, 0);

    // Add click event
    lv_obj_add_event_cb(btn, thumbnail_clicked_callback_, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btn, (void *)(intptr_t)i);

    this->thumbnail_buttons_.push_back(btn);

    // Create canvas for camera stream inside button
    lv_obj_t *canvas = lv_canvas_create(btn);
    lv_obj_set_size(canvas, lv_pct(100), lv_pct(100));
    lv_obj_align(canvas, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(canvas, lv_color_hex(0x4682B4), 0);

    this->thumbnail_canvases_.push_back(canvas);

    // Configure camera to use this canvas
    if (i < this->cameras_.size()) {
      this->cameras_[i]->configure_canvas(canvas);
    }

    // Add camera label overlay
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text_fmt(label, "Camera %d", i + 1);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 5, 5);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(label, 3, 0);
    lv_obj_set_style_radius(label, 3, 0);
  }

  ESP_LOGI(TAG, "Grid layout created with %d cameras", num_cameras);
}

void MultiCameraDisplay::setup_fullscreen_layout_() {
  if (this->canvas_obj_ == nullptr) {
    return;
  }

  // Create fullscreen canvas (hidden by default)
  this->fullscreen_canvas_ = lv_canvas_create(this->canvas_obj_);
  lv_obj_set_size(this->fullscreen_canvas_, lv_pct(100), lv_pct(100));
  lv_obj_set_pos(this->fullscreen_canvas_, 0, 0);
  lv_obj_set_style_bg_color(this->fullscreen_canvas_, lv_color_hex(0x000000), 0);
  lv_obj_add_flag(this->fullscreen_canvas_, LV_OBJ_FLAG_HIDDEN);

  // Create back button
  this->back_button_ = lv_btn_create(this->canvas_obj_);
  lv_obj_set_size(this->back_button_, 80, 60);
  lv_obj_align(this->back_button_, LV_ALIGN_TOP_RIGHT, -10, 10);
  lv_obj_set_style_bg_color(this->back_button_, lv_color_hex(0xFF4444), 0);
  lv_obj_set_style_radius(this->back_button_, 10, 0);
  lv_obj_add_event_cb(this->back_button_, back_button_callback_, LV_EVENT_CLICKED, this);
  lv_obj_add_flag(this->back_button_, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *label = lv_label_create(this->back_button_);
  lv_label_set_text(label, "BACK");
  lv_obj_center(label);
  lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);

  ESP_LOGI(TAG, "Fullscreen layout created");
}

void MultiCameraDisplay::show_grid_() {
  if (this->grid_container_ == nullptr) {
    return;
  }

  ESP_LOGI(TAG, "Showing grid view");

  // Hide fullscreen
  if (this->fullscreen_canvas_ != nullptr) {
    lv_obj_add_flag(this->fullscreen_canvas_, LV_OBJ_FLAG_HIDDEN);
  }
  if (this->back_button_ != nullptr) {
    lv_obj_add_flag(this->back_button_, LV_OBJ_FLAG_HIDDEN);
  }

  // Show grid
  lv_obj_clear_flag(this->grid_container_, LV_OBJ_FLAG_HIDDEN);

  // Reconfigure cameras to use thumbnail canvases
  for (size_t i = 0; i < this->cameras_.size() && i < this->thumbnail_canvases_.size(); i++) {
    this->cameras_[i]->configure_canvas(this->thumbnail_canvases_[i]);
  }

  this->mode_ = DisplayMode::GRID;
  this->fullscreen_camera_index_ = -1;
}

void MultiCameraDisplay::show_fullscreen_(int camera_index) {
  if (camera_index < 0 || camera_index >= this->cameras_.size()) {
    ESP_LOGE(TAG, "Invalid camera index: %d", camera_index);
    return;
  }

  ESP_LOGI(TAG, "Showing fullscreen view for camera %d", camera_index);

  // Hide grid
  if (this->grid_container_ != nullptr) {
    lv_obj_add_flag(this->grid_container_, LV_OBJ_FLAG_HIDDEN);
  }

  // Show fullscreen
  if (this->fullscreen_canvas_ != nullptr) {
    lv_obj_clear_flag(this->fullscreen_canvas_, LV_OBJ_FLAG_HIDDEN);
  }
  if (this->back_button_ != nullptr) {
    lv_obj_clear_flag(this->back_button_, LV_OBJ_FLAG_HIDDEN);
  }

  // Configure camera to use fullscreen canvas
  this->cameras_[camera_index]->configure_canvas(this->fullscreen_canvas_);

  this->mode_ = DisplayMode::FULLSCREEN;
  this->fullscreen_camera_index_ = camera_index;
}

void MultiCameraDisplay::thumbnail_clicked_callback_(lv_event_t *e) {
  MultiCameraDisplay *display = (MultiCameraDisplay *)lv_event_get_user_data(e);
  lv_obj_t *btn = lv_event_get_target(e);
  int camera_index = (int)(intptr_t)lv_obj_get_user_data(btn);

  ESP_LOGI(TAG, "Thumbnail %d clicked", camera_index);
  display->show_fullscreen_(camera_index);
}

void MultiCameraDisplay::back_button_callback_(lv_event_t *e) {
  MultiCameraDisplay *display = (MultiCameraDisplay *)lv_event_get_user_data(e);

  ESP_LOGI(TAG, "Back button clicked");
  display->show_grid_();
}

}  // namespace multi_camera_display
}  // namespace esphome
