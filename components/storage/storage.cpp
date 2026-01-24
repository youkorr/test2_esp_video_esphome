#include "storage.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"  // For App.feed_wdt()
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <algorithm>

// LVGL integration for triggering display updates
#ifdef USE_LVGL
#include "esphome/components/lvgl/lvgl_esphome.h"
#endif

// Include yield function for ESP32/ESP8266
#ifdef ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#define yield() taskYIELD()
#elif defined(ESP8266)
#include <Esp.h>
// yield() is already available on ESP8266
#else
// Fallback for other platforms
#define yield() delayMicroseconds(1)
#endif

namespace esphome {
namespace storage {

static const char *const TAG = "storage";
static const char *const TAG_IMAGE = "storage.image";

// Global decoder instances for callbacks
static SdImageComponent *current_image_component = nullptr;

// =====================================================
// StorageComponent Implementation
// =====================================================

void StorageComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Storage Component...");
  ESP_LOGCONFIG(TAG, "  Platform: %s", this->platform_.c_str());
  ESP_LOGCONFIG(TAG, "  Root path: %s", this->root_path_.c_str());
  ESP_LOGCONFIG(TAG, "  SD component: %s", this->sd_component_ ? "configured" : "not configured");
}

void StorageComponent::loop() {
  // Nothing to do in loop
}

void StorageComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Storage Component:");
  ESP_LOGCONFIG(TAG, "  Platform: %s", this->platform_.c_str());
  ESP_LOGCONFIG(TAG, "  Root path: %s", this->root_path_.c_str());
  ESP_LOGCONFIG(TAG, "  SD component: %s", this->sd_component_ ? "YES" : "NO");
}

bool StorageComponent::file_exists_direct(const std::string &path) {
  std::string full_path = this->root_path_ + path;
  struct stat st;
  return stat(full_path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::vector<uint8_t> StorageComponent::read_file_direct(const std::string &path) {
  std::string full_path = this->root_path_ + path;
  FILE *file = fopen(full_path.c_str(), "rb");
  
  if (!file) {
    ESP_LOGE(TAG, "Failed to open file: %s (errno: %d)", full_path.c_str(), errno);
    return {};
  }
  
  // Get file size safely
  if (fseek(file, 0, SEEK_END) != 0) {
    ESP_LOGE(TAG, "Failed to seek to end of file: %s", full_path.c_str());
    fclose(file);
    return {};
  }
  
  long size = ftell(file);
  if (size < 0 || size > 10 * 1024 * 1024) { // 10MB limit
    ESP_LOGE(TAG, "Invalid file size: %ld bytes", size);
    fclose(file);
    return {};
  }
  
  if (fseek(file, 0, SEEK_SET) != 0) {
    ESP_LOGE(TAG, "Failed to seek to beginning of file: %s", full_path.c_str());
    fclose(file);
    return {};
  }
  
  std::vector<uint8_t> data(size);
  size_t read_size = fread(data.data(), 1, size, file);
  fclose(file);
  
  if (read_size != static_cast<size_t>(size)) {
    ESP_LOGE(TAG, "Failed to read complete file: expected %ld, got %zu", size, read_size);
    return {};
  }
  
  return data;
}

bool StorageComponent::write_file_direct(const std::string &path, const std::vector<uint8_t> &data) {
  std::string full_path = this->root_path_ + path;
  FILE *file = fopen(full_path.c_str(), "wb");
  
  if (!file) {
    ESP_LOGE(TAG, "Failed to create file: %s", full_path.c_str());
    return false;
  }
  
  size_t written = fwrite(data.data(), 1, data.size(), file);
  fclose(file);
  
  return written == data.size();
}

size_t StorageComponent::get_file_size(const std::string &path) {
  std::string full_path = this->root_path_ + path;
  struct stat st;
  if (stat(full_path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
    return st.st_size;
  }
  return 0;
}

// =====================================================
// SdImageComponent Implementation  
// =====================================================

void SdImageComponent::setup() {
  ESP_LOGCONFIG(TAG_IMAGE, "Setting up SD Image Component...");
  ESP_LOGCONFIG(TAG_IMAGE, "  File path: %s", this->file_path_.c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Resize: %dx%d", this->resize_width_, this->resize_height_);
  ESP_LOGCONFIG(TAG_IMAGE, "  Format: %s", this->format_to_string().c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Auto load: %s", this->auto_load_ ? "YES" : "NO");
  ESP_LOGCONFIG(TAG_IMAGE, "  Storage component: %s", this->storage_component_ ? "configured" : "not configured");
  ESP_LOGCONFIG(TAG_IMAGE, "  Decoders: JPEG available");
  
  // CRITIQUE: Chargement automatique avec plus de vérifications
  if (this->auto_load_) {
    if (this->file_path_.empty()) {
      ESP_LOGW(TAG_IMAGE, "Auto-load enabled but no file path configured!");
      return;
    }
    
    if (!this->storage_component_) {
      ESP_LOGW(TAG_IMAGE, "Auto-load enabled but no storage component configured!");
      return;
    }
    
    // Attendre un peu que la carte SD soit prête
    ESP_LOGI(TAG_IMAGE, "Waiting for SD card to be ready...");
    delay(500); // Attendre 500ms
    
    ESP_LOGI(TAG_IMAGE, "Auto-loading image from: %s", this->file_path_.c_str());
    if (this->load_image()) {
      ESP_LOGI(TAG_IMAGE, "Image auto-loaded successfully!");
    } else {
      ESP_LOGW(TAG_IMAGE, "Failed to auto-load image, will retry later");
      // Programmer un retry dans la loop()
      this->retry_load_ = true;
    }
  }
}

void SdImageComponent::loop() {
  // CRITICAL: Libération différée de la PSRAM si programmée (évite crash pendant rendering LVGL)
  if (this->pending_unload_time_ > 0 && millis() >= this->pending_unload_time_) {
    ESP_LOGI(TAG_IMAGE, "Unloading image (delayed free_after_draw)");
    this->unload_image();
    this->pending_unload_time_ = 0;  // Reset
  }

  // Handle GIF animation
  if (!this->is_gif_animated_ || this->gif_frames_.empty()) {
    return;  // Not an animated GIF or no frames
  }

  // Check if it's time to advance to next frame
  uint32_t now = millis();

  // Initialize last_frame_time_ on first call
  if (this->last_frame_time_ == 0) {
    this->last_frame_time_ = now;
    return;
  }

  const GifFrame &current_frame = this->gif_frames_[this->current_gif_frame_];
  uint32_t elapsed = now - this->last_frame_time_;

  if (elapsed >= current_frame.delay_ms) {
    // Advance to next frame
    this->current_gif_frame_++;
    if (this->current_gif_frame_ >= this->gif_frames_.size()) {
      this->current_gif_frame_ = 0;  // Loop back to first frame
    }

    // Copy new frame to image_buffer_
    const GifFrame &next_frame = this->gif_frames_[this->current_gif_frame_];
    memcpy(this->image_buffer_.data(), next_frame.pixels.data(), this->image_buffer_.size());

    // Update base Image class pointer
    this->data_start_ = this->image_buffer_.data();

    // Update timestamp
    this->last_frame_time_ = now;

    // Trigger LVGL display update so the new frame is rendered
    #ifdef USE_LVGL
    // Feed watchdog before potentially long operation
    App.feed_wdt();

    // Invalidate the screen to trigger redraw
    // Note: lv_image_cache_drop() was removed as it's not available in LVGL 9.4
    // The screen invalidation is sufficient to trigger image redraw from the updated buffer
    lv_obj_t *screen = lv_scr_act();
    if (screen != nullptr) {
      lv_obj_invalidate(screen);
    }
    #endif

    // Log frame change (only occasionally to avoid spam)
    if (this->current_gif_frame_ == 0) {
      ESP_LOGD(TAG_IMAGE, "GIF animation looped back to frame 0");
    }
  }
}

void SdImageComponent::dump_config() {
  ESP_LOGCONFIG(TAG_IMAGE, "SD Image Component:");
  ESP_LOGCONFIG(TAG_IMAGE, "  File: %s", this->file_path_.c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Dimensions: %dx%d", this->image_width_, this->image_height_);
  ESP_LOGCONFIG(TAG_IMAGE, "  Format: %s", this->format_to_string().c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Loaded: %s", this->image_loaded_ ? "YES" : "NO");
  if (this->image_loaded_) {
    ESP_LOGCONFIG(TAG_IMAGE, "  Buffer size: %zu bytes", this->image_buffer_.size());
    ESP_LOGCONFIG(TAG_IMAGE, "  Base Image - W:%d H:%d Type:%d Data:%p",
                  this->width_, this->height_, this->type_, this->data_start_);
    if (this->is_gif_animated_) {
      ESP_LOGCONFIG(TAG_IMAGE, "  GIF Frames: %zu, Animated: YES", this->gif_frames_.size());
    }
  }
}

// =====================================================
// GIF Animation Control Methods
// =====================================================

void SdImageComponent::set_frame(size_t frame_index) {
  if (this->gif_frames_.empty()) return;

  this->current_gif_frame_ = frame_index % this->gif_frames_.size();

  // Copy frame to image buffer
  const GifFrame &frame = this->gif_frames_[this->current_gif_frame_];
  if (frame.pixels.size() <= this->image_buffer_.size()) {
    memcpy(this->image_buffer_.data(), frame.pixels.data(), frame.pixels.size());
    this->data_start_ = this->image_buffer_.data();
  }
}

void SdImageComponent::next_frame() {
  if (this->gif_frames_.empty()) return;
  this->set_frame(this->current_gif_frame_ + 1);
}

void SdImageComponent::prev_frame() {
  if (this->gif_frames_.empty()) return;
  if (this->current_gif_frame_ == 0) {
    this->set_frame(this->gif_frames_.size() - 1);
  } else {
    this->set_frame(this->current_gif_frame_ - 1);
  }
}

uint16_t SdImageComponent::get_frame_delay() const {
  if (this->gif_frames_.empty()) return 100;
  return this->gif_frames_[this->current_gif_frame_].delay_ms;
}

// =====================================================
// LVGL Canvas Drawing Support
// =====================================================

#ifdef USE_LVGL

void SdImageComponent::draw_to_canvas(lv_obj_t *canvas, int x, int y) {
  if (!this->image_loaded_ || this->image_buffer_.empty()) {
    ESP_LOGW(TAG_IMAGE, "Cannot draw to canvas: image not loaded");
    return;
  }

  if (canvas == nullptr) {
    ESP_LOGW(TAG_IMAGE, "Cannot draw to canvas: canvas is null");
    return;
  }

  // Get canvas dimensions for bounds checking
  lv_coord_t canvas_w = lv_obj_get_width(canvas);
  lv_coord_t canvas_h = lv_obj_get_height(canvas);

  if (canvas_w <= 0 || canvas_h <= 0) {
    ESP_LOGW(TAG_IMAGE, "Canvas has invalid dimensions: %dx%d", canvas_w, canvas_h);
    return;
  }

  int img_w = this->get_current_width();
  int img_h = this->get_current_height();

  // Log only occasionally to avoid spam
  static uint32_t last_log_time = 0;
  uint32_t now = millis();
  if (now - last_log_time > 5000) {  // Log every 5 seconds max
    ESP_LOGD(TAG_IMAGE, "Drawing to canvas: img=%dx%d, canvas=%dx%d, pos=(%d,%d)",
             img_w, img_h, canvas_w, canvas_h, x, y);
    last_log_time = now;
  }

  // Get transparency mask for current frame (if GIF animation)
  const std::vector<bool> *transparency_mask = nullptr;
  bool has_transparency = false;
  if (!this->gif_frames_.empty() && this->current_gif_frame_ < this->gif_frames_.size()) {
    const GifFrame &frame = this->gif_frames_[this->current_gif_frame_];
    if (frame.has_transparency && !frame.transparency.empty()) {
      transparency_mask = &frame.transparency;
      has_transparency = true;
    }
  }

  // Draw pixels directly to canvas
  // API differs between LVGL v8 and v9
  for (int py = 0; py < img_h; py++) {
    for (int px = 0; px < img_w; px++) {
      int canvas_x = x + px;
      int canvas_y = y + py;

      // Bounds check - CRITICAL to prevent memory access fault
      if (canvas_x < 0 || canvas_x >= canvas_w || canvas_y < 0 || canvas_y >= canvas_h) {
        continue;  // Skip pixels outside canvas bounds
      }

      size_t pixel_idx = py * img_w + px;

      // Skip transparent pixels - they keep the canvas background
      if (has_transparency && transparency_mask && pixel_idx < transparency_mask->size()) {
        if ((*transparency_mask)[pixel_idx]) {
          continue;  // Skip this pixel, it's transparent
        }
      }

      // Get RGB565 pixel from our buffer
      size_t offset = pixel_idx * 2;
      if (offset + 1 >= this->image_buffer_.size()) continue;

      uint16_t rgb565 = this->image_buffer_[offset] | (this->image_buffer_[offset + 1] << 8);

      // Convert RGB565 to lv_color_t
      uint8_t r = ((rgb565 >> 11) & 0x1F) << 3;
      uint8_t g = ((rgb565 >> 5) & 0x3F) << 2;
      uint8_t b = (rgb565 & 0x1F) << 3;

      lv_color_t color = lv_color_make(r, g, b);

      // LVGL v8 vs v9 API difference
      #if LVGL_VERSION_MAJOR >= 9
      lv_canvas_set_px(canvas, canvas_x, canvas_y, color, LV_OPA_COVER);
      #else
      // LVGL v8: lv_canvas_set_px takes only 4 arguments
      lv_canvas_set_px(canvas, canvas_x, canvas_y, color);
      #endif
    }

    // Feed watchdog periodically
    if (py % 32 == 0) {
      App.feed_wdt();
    }
  }

  // Invalidate canvas to trigger redraw
  lv_obj_invalidate(canvas);

  // CRITICAL: Programmer la libération de la PSRAM après un délai (pour éviter crash pendant rendering LVGL)
  if (this->free_after_draw_ && !this->is_gif_animated_ && this->image_loaded_) {
    ESP_LOGI(TAG_IMAGE, "Scheduling PSRAM release after draw (will free in 100ms to allow LVGL rendering)");
    this->pending_unload_time_ = millis() + 100;  // Libérer dans 100ms
  }
}

bool SdImageComponent::update_canvas_animation(lv_obj_t *canvas, int x, int y) {
  if (!this->is_gif_animated_ || this->gif_frames_.empty()) {
    return false;
  }

  uint32_t now = millis();

  // Initialize timing on first call
  if (this->last_frame_time_ == 0) {
    this->last_frame_time_ = now;
    // Clear canvas before first draw if needed
    const GifFrame &first_frame = this->gif_frames_[0];
    if (first_frame.has_transparency) {
      this->clear_canvas_area(canvas, x, y);
    }
    this->draw_to_canvas(canvas, x, y);
    return true;
  }

  // Check if it's time to advance frame
  const GifFrame &current_frame = this->gif_frames_[this->current_gif_frame_];
  uint32_t elapsed = now - this->last_frame_time_;

  if (elapsed >= current_frame.delay_ms) {
    // Advance to next frame
    this->next_frame();
    this->last_frame_time_ = now;

    // Only clear canvas if the new frame has transparency
    // Fully opaque frames will overwrite everything anyway
    const GifFrame &new_frame = this->gif_frames_[this->current_gif_frame_];
    if (new_frame.has_transparency) {
      this->clear_canvas_area(canvas, x, y);
    }

    // Draw new frame to canvas
    this->draw_to_canvas(canvas, x, y);
    return true;
  }

  return false;
}

void SdImageComponent::clear_canvas_area(lv_obj_t *canvas, int x, int y) {
  if (canvas == nullptr) return;

  // Use lv_canvas_fill_bg for efficient clearing (LVGL v8)
  #if LVGL_VERSION_MAJOR >= 9
  // LVGL v9 - use fill
  lv_color_t bg_color = lv_obj_get_style_bg_color(canvas, LV_PART_MAIN);
  lv_canvas_fill_bg(canvas, bg_color, LV_OPA_COVER);
  #else
  // LVGL v8 - use fill_bg with 3 params
  lv_color_t bg_color = lv_obj_get_style_bg_color(canvas, LV_PART_MAIN);
  lv_canvas_fill_bg(canvas, bg_color, LV_OPA_COVER);
  #endif
}

#endif  // USE_LVGL

// Compatibility methods for YAML configuration
void SdImageComponent::set_output_format_string(const std::string &format) {
  if (format == "RGB565") {
    this->format_ = ImageFormat::RGB565;
  } else if (format == "RGB888") {
    this->format_ = ImageFormat::RGB888;
  } else if (format == "RGBA") {
    this->format_ = ImageFormat::RGBA;
  } else {
    ESP_LOGW(TAG_IMAGE, "Unknown format: %s, using RGB565", format.c_str());
    this->format_ = ImageFormat::RGB565;
  }
}

void SdImageComponent::set_byte_order_string(const std::string &byte_order) {
  ESP_LOGD(TAG_IMAGE, "Byte order set to: %s", byte_order.c_str());
}

// Implementation de la méthode draw() selon le code source ESPHome
void SdImageComponent::draw(int x, int y, display::Display *display, Color color_on, Color color_off) {
  if (!this->image_loaded_ || this->image_buffer_.empty()) {
    ESP_LOGW(TAG_IMAGE, "Cannot draw: image not loaded");
    return;
  }
  
  ESP_LOGD(TAG_IMAGE, "Drawing SD image %dx%d at position %d,%d (Base: W:%d H:%d Data:%p)", 
           this->get_current_width(), this->get_current_height(), x, y,
           this->width_, this->height_, this->data_start_);
  
  // Si les données de base sont correctes, utiliser la méthode optimisée d'ESPHome
  if (this->data_start_ && this->width_ > 0 && this->height_ > 0) {
    ESP_LOGD(TAG_IMAGE, "Using ESPHome base image draw method");
    // Appeler la méthode de base qui gère le clipping et l'optimisation
    image::Image::draw(x, y, display, color_on, color_off);
  } else {
    ESP_LOGD(TAG_IMAGE, "Using fallback pixel-by-pixel drawing");
    // Fallback: dessiner pixel par pixel
    this->draw_pixels_directly(x, y, display, color_on, color_off);
  }
}

// Loading methods
bool SdImageComponent::load_image() {
  return this->load_image_from_path(this->file_path_);
}

bool SdImageComponent::load_image_from_path(const std::string &path) {
  ESP_LOGI(TAG_IMAGE, "Loading image from: %s", path.c_str());
  
  if (!this->storage_component_) {
    ESP_LOGE(TAG_IMAGE, "Storage component not available");
    return false;
  }
  
  // Unload previous image
  this->unload_image();
  
  // Check file existence
  if (!this->storage_component_->file_exists_direct(path)) {
    ESP_LOGE(TAG_IMAGE, "Image file not found: %s", path.c_str());
    
    // List directory for debugging
    std::string dir_path = path.substr(0, path.find_last_of("/"));
    if (dir_path.empty()) dir_path = "/";
    this->list_directory_contents(dir_path);
    
    return false;
  }
  
  // Read file data
  std::vector<uint8_t> file_data = this->storage_component_->read_file_direct(path);
  if (file_data.empty()) {
    ESP_LOGE(TAG_IMAGE, "Failed to read image file: %s", path.c_str());
    return false;
  }
  
  ESP_LOGI(TAG_IMAGE, "Read %zu bytes from file", file_data.size());
  
  // Show first few bytes for debugging
  if (file_data.size() >= 16) {
    ESP_LOGI(TAG_IMAGE, "First 16 bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", 
             file_data[0], file_data[1], file_data[2], file_data[3],
             file_data[4], file_data[5], file_data[6], file_data[7],
             file_data[8], file_data[9], file_data[10], file_data[11],
             file_data[12], file_data[13], file_data[14], file_data[15]);
  }
  
  // Decode image
  if (!this->decode_image(file_data)) {
    ESP_LOGE(TAG_IMAGE, "Failed to decode image: %s", path.c_str());
    return false;
  }
  
  this->file_path_ = path;
  this->image_loaded_ = true;
  
  // Finaliser le chargement en mettant à jour les propriétés de base
  this->finalize_image_load();
  
  ESP_LOGI(TAG_IMAGE, "Image loaded successfully: %dx%d, %zu bytes", 
           this->image_width_, this->image_height_, this->image_buffer_.size());
  
  return true;
}

void SdImageComponent::unload_image() {
  // Log avant libération pour débuggage
  size_t image_buffer_size = this->image_buffer_.size();
  size_t gif_frames_count = this->gif_frames_.size();
  size_t total_gif_memory = 0;

  for (const auto &frame : this->gif_frames_) {
    total_gif_memory += frame.pixels.size() + frame.transparency.size();
  }

  if (image_buffer_size > 0 || total_gif_memory > 0) {
    ESP_LOGI(TAG_IMAGE, "Unloading image - Freeing memory:");
    ESP_LOGI(TAG_IMAGE, "  image_buffer_: %zu bytes", image_buffer_size);
    ESP_LOGI(TAG_IMAGE, "  gif_frames_: %zu frames, %zu bytes total", gif_frames_count, total_gif_memory);
    ESP_LOGI(TAG_IMAGE, "  TOTAL PSRAM to free: %zu bytes (~%.2f MB)",
             image_buffer_size + total_gif_memory,
             (image_buffer_size + total_gif_memory) / (1024.0 * 1024.0));
  }

  // CRITIQUE: Libérer image_buffer_ (buffer principal)
  this->image_buffer_.clear();
  this->image_buffer_.shrink_to_fit();

  // CRITIQUE: Libérer gif_frames_ (frames d'animation GIF - PEUT ÊTRE TRÈS GROS!)
  // Chaque frame contient pixels (RGB565) + transparency mask
  // Pour un GIF 320x240 avec 60 frames = ~9 MB de PSRAM!
  for (auto &frame : this->gif_frames_) {
    frame.pixels.clear();
    frame.pixels.shrink_to_fit();
    frame.transparency.clear();
    frame.transparency.shrink_to_fit();
  }
  this->gif_frames_.clear();
  this->gif_frames_.shrink_to_fit();

  // Réinitialiser les états d'animation GIF
  this->is_gif_animated_ = false;
  this->current_gif_frame_ = 0;
  this->last_frame_time_ = 0;

  // Réinitialiser les flags et dimensions
  this->image_loaded_ = false;
  this->image_width_ = 0;
  this->image_height_ = 0;

  // Réinitialiser les propriétés de la classe de base ESPHome Image
  this->width_ = 0;
  this->height_ = 0;
  this->data_start_ = nullptr;
  this->bpp_ = 0;

  // Reset pending unload timer
  this->pending_unload_time_ = 0;

  ESP_LOGD(TAG_IMAGE, "Image unloaded - PSRAM freed successfully");
}

bool SdImageComponent::reload_image() {
  std::string path = this->file_path_;
  this->unload_image();
  return this->load_image_from_path(path);
}

// Implémentation de finalize_image_load()
void SdImageComponent::finalize_image_load() {
  if (this->image_loaded_) {
    this->update_base_image_properties();
    ESP_LOGI(TAG_IMAGE, "Image properties updated - W:%d H:%d Type:%d Data:%p BPP:%d", 
             this->width_, this->height_, this->type_, this->data_start_, this->bpp_);
  }
}

// Mise à jour des propriétés de la classe de base selon le code source ESPHome
void SdImageComponent::update_base_image_properties() {
  // Mettre à jour les membres de la classe de base
  this->width_ = this->get_current_width();
  this->height_ = this->get_current_height();
  this->type_ = this->get_esphome_image_type();
  
  if (!this->image_buffer_.empty()) {
    this->data_start_ = this->image_buffer_.data();
    
    // Calculer bpp selon le code source ESPHome
    switch (this->type_) {
      case image::IMAGE_TYPE_BINARY:
        this->bpp_ = 1;
        break;
      case image::IMAGE_TYPE_GRAYSCALE:
        this->bpp_ = 8;
        break;
      case image::IMAGE_TYPE_RGB565:
        this->bpp_ = 16;
        break;
      case image::IMAGE_TYPE_RGB:
        this->bpp_ = 24;
        break;
      default:
        this->bpp_ = 16;
        break;
    }
  } else {
    this->data_start_ = nullptr;
    this->bpp_ = 0;
  }
}

int SdImageComponent::get_current_width() const {
  return this->resize_width_ > 0 ? this->resize_width_ : this->image_width_;
}

int SdImageComponent::get_current_height() const {
  return this->resize_height_ > 0 ? this->resize_height_ : this->image_height_;
}

image::ImageType SdImageComponent::get_esphome_image_type() const {
  switch (this->format_) {
    case ImageFormat::RGB565: return image::IMAGE_TYPE_RGB565;
    case ImageFormat::RGB888: return image::IMAGE_TYPE_RGB;
    case ImageFormat::RGBA: return image::IMAGE_TYPE_RGB; // ESPHome n'a pas de RGBA natif
    default: return image::IMAGE_TYPE_RGB565;
  }
}

void SdImageComponent::draw_pixels_directly(int x, int y, display::Display *display, Color color_on, Color color_off) {
  // Méthode de fallback pour dessiner directement
  ESP_LOGD(TAG_IMAGE, "Drawing %dx%d pixels directly", this->get_current_width(), this->get_current_height());
  
  for (int img_y = 0; img_y < this->get_current_height(); img_y++) {
    for (int img_x = 0; img_x < this->get_current_width(); img_x++) {
      Color pixel_color = this->get_pixel_color(img_x, img_y);
      display->draw_pixel_at(x + img_x, y + img_y, pixel_color);
    }
    
    // Yield périodique pour éviter le watchdog
    if (img_y % 32 == 0) {
      App.feed_wdt();
      yield();
    }
  }
}

void SdImageComponent::draw_pixel_at(display::Display *display, int screen_x, int screen_y, int img_x, int img_y) {
  Color pixel_color = this->get_pixel_color(img_x, img_y);
  display->draw_pixel_at(screen_x, screen_y, pixel_color);
}

Color SdImageComponent::get_pixel_color(int x, int y) const {
  if (x < 0 || x >= this->get_current_width() || y < 0 || y >= this->get_current_height()) {
    return Color::BLACK;
  }
  
  size_t offset = (y * this->get_current_width() + x) * this->get_pixel_size();
  
  if (offset + this->get_pixel_size() > this->image_buffer_.size()) {
    return Color::BLACK;
  }
  
  switch (this->format_) {
    case ImageFormat::RGB565: {
      uint16_t rgb565 = this->image_buffer_[offset] | (this->image_buffer_[offset + 1] << 8);
      uint8_t r = ((rgb565 >> 11) & 0x1F) << 3;
      uint8_t g = ((rgb565 >> 5) & 0x3F) << 2;
      uint8_t b = (rgb565 & 0x1F) << 3;
      return Color(r, g, b);
    }
    case ImageFormat::RGB888:
      return Color(this->image_buffer_[offset], 
                  this->image_buffer_[offset + 1], 
                  this->image_buffer_[offset + 2]);
    case ImageFormat::RGBA:
      return Color(this->image_buffer_[offset], 
                  this->image_buffer_[offset + 1], 
                  this->image_buffer_[offset + 2], 
                  this->image_buffer_[offset + 3]);
    default:
      return Color::BLACK;
  }
}

// File type detection
SdImageComponent::FileType SdImageComponent::detect_file_type(const std::vector<uint8_t> &data) const {
  if (this->is_jpeg_data(data)) return FileType::JPEG;
  if (this->is_gif_data(data)) return FileType::GIF;
  if (this->is_png_data(data)) return FileType::PNG;
  if (this->is_bmp_data(data)) return FileType::BMP;
  if (this->is_svg_data(data)) return FileType::SVG;
  if (this->is_lottie_data(data)) return FileType::LOTTIE;
  return FileType::UNKNOWN;
}

bool SdImageComponent::is_jpeg_data(const std::vector<uint8_t> &data) const {
  return data.size() >= 4 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

bool SdImageComponent::is_gif_data(const std::vector<uint8_t> &data) const {
  // Check for GIF87a or GIF89a signature
  return data.size() >= 6 &&
         data[0] == 'G' && data[1] == 'I' && data[2] == 'F' &&
         data[3] == '8' && (data[4] == '7' || data[4] == '9') && data[5] == 'a';
}

bool SdImageComponent::is_png_data(const std::vector<uint8_t> &data) const {
  // PNG signature: 0x89 'P' 'N' 'G' 0x0D 0x0A 0x1A 0x0A
  return data.size() >= 8 &&
         data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G' &&
         data[4] == 0x0D && data[5] == 0x0A && data[6] == 0x1A && data[7] == 0x0A;
}

bool SdImageComponent::is_bmp_data(const std::vector<uint8_t> &data) const {
  // BMP signature: 'B' 'M' at start
  return data.size() >= 2 && data[0] == 'B' && data[1] == 'M';
}

bool SdImageComponent::is_svg_data(const std::vector<uint8_t> &data) const {
  // SVG is XML, starts with '<' and contains "svg"
  if (data.size() < 10) return false;
  // Check for "<?xml" or "<svg"
  if (data[0] == '<') {
    std::string start(data.begin(), data.begin() + std::min(data.size(), size_t(100)));
    return start.find("svg") != std::string::npos;
  }
  return false;
}

bool SdImageComponent::is_lottie_data(const std::vector<uint8_t> &data) const {
  // Lottie is JSON, typically starts with '{' and contains "v" (version) or "assets"
  if (data.size() < 10) return false;
  if (data[0] == '{') {
    std::string start(data.begin(), data.begin() + std::min(data.size(), size_t(100)));
    // Lottie JSON contains specific keys
    return (start.find("\"v\"") != std::string::npos ||
            start.find("\"assets\"") != std::string::npos ||
            start.find("\"layers\"") != std::string::npos);
  }
  return false;
}

// Image decoding
bool SdImageComponent::decode_image(const std::vector<uint8_t> &data) {
  FileType type = this->detect_file_type(data);

  switch (type) {
    case FileType::JPEG:
      ESP_LOGI(TAG_IMAGE, "Decoding JPEG image");
      return this->decode_jpeg_image(data);

    case FileType::GIF:
      ESP_LOGI(TAG_IMAGE, "Decoding GIF image");
      return this->decode_gif_image(data);

    case FileType::PNG:
      ESP_LOGI(TAG_IMAGE, "Decoding PNG image");
      return this->decode_png_image(data);

    case FileType::BMP:
      ESP_LOGI(TAG_IMAGE, "Decoding BMP image");
      return this->decode_bmp_image(data);

    case FileType::SVG:
      ESP_LOGI(TAG_IMAGE, "Decoding SVG image");
      return this->decode_svg_image(data);

    case FileType::LOTTIE:
      ESP_LOGI(TAG_IMAGE, "Decoding Lottie animation");
      return this->decode_lottie_image(data);

    default:
      ESP_LOGE(TAG_IMAGE, "Unsupported image format (supported: JPEG, GIF, PNG, BMP, SVG, Lottie)");
      return false;
  }
}

// =====================================================
// JPEG Decoder Implementation (ESPHome style)
// =====================================================

bool SdImageComponent::decode_jpeg_image(const std::vector<uint8_t> &jpeg_data) {
  ESP_LOGD(TAG_IMAGE, "Using JPEGDEC decoder");
  
  // Set current component for callback
  current_image_component = this;
  
  // Create decoder
  this->jpeg_decoder_ = new JPEGDEC();
  if (!this->jpeg_decoder_) {
    ESP_LOGE(TAG_IMAGE, "Failed to allocate JPEG decoder");
    current_image_component = nullptr;
    return false;
  }
  
  // Configuration correcte du décodeur JPEGDEC
  // Forcer le format RGB565 directement dans JPEGDEC
  this->format_ = ImageFormat::RGB565;
  
  // Open JPEG avec validation
  int result = this->jpeg_decoder_->openRAM((uint8_t*)jpeg_data.data(), jpeg_data.size(), 
                                           SdImageComponent::jpeg_decode_callback);
  if (result != 1) {
    ESP_LOGE(TAG_IMAGE, "Failed to open JPEG data: %d", result);
    delete this->jpeg_decoder_;
    this->jpeg_decoder_ = nullptr;
    current_image_component = nullptr;
    return false;
  }
  
  // Get image dimensions
  int orig_width = this->jpeg_decoder_->getWidth();
  int orig_height = this->jpeg_decoder_->getHeight();
  
  ESP_LOGI(TAG_IMAGE, "JPEG original dimensions: %dx%d", orig_width, orig_height);
  
  // Validate dimensions
  if (orig_width <= 0 || orig_height <= 0 || 
      orig_width > 2048 || orig_height > 2048) {
    ESP_LOGE(TAG_IMAGE, "Invalid JPEG dimensions: %dx%d", orig_width, orig_height);
    this->jpeg_decoder_->close();
    delete this->jpeg_decoder_;
    this->jpeg_decoder_ = nullptr;
    current_image_component = nullptr;
    return false;
  }
  
  // Gestion correcte du redimensionnement
  if (this->resize_width_ > 0 && this->resize_height_ > 0) {
    this->image_width_ = this->resize_width_;
    this->image_height_ = this->resize_height_;
    ESP_LOGI(TAG_IMAGE, "Will resize to: %dx%d", this->image_width_, this->image_height_);
  } else {
    this->image_width_ = orig_width;
    this->image_height_ = orig_height;
  }
  
  // Allocate buffer
  if (!this->allocate_image_buffer()) {
    this->jpeg_decoder_->close();
    delete this->jpeg_decoder_;
    this->jpeg_decoder_ = nullptr;
    current_image_component = nullptr;
    return false;
  }
  
  // Initialisation propre du buffer
  std::fill(this->image_buffer_.begin(), this->image_buffer_.end(), 0);
  
  ESP_LOGI(TAG_IMAGE, "Starting JPEG decode to RGB565...");
  
  // Paramètres de décodage optimisés
  // decode(x, y, flags)
  int decode_flags = 0;
  
  result = this->jpeg_decoder_->decode(0, 0, decode_flags);
  
  // Cleanup
  this->jpeg_decoder_->close();
  delete this->jpeg_decoder_;
  this->jpeg_decoder_ = nullptr;
  current_image_component = nullptr;
  
  if (result != 1) {
    ESP_LOGE(TAG_IMAGE, "Failed to decode JPEG: %d", result);
    return false;
  }
  
  ESP_LOGI(TAG_IMAGE, "JPEG decoded successfully to RGB565 format");
  
  // Validation finale
  if (this->image_buffer_.empty()) {
    ESP_LOGE(TAG_IMAGE, "Image buffer is empty after decoding");
    return false;
  }
  
  // Log amélioré pour debug
  if (this->image_buffer_.size() >= 8) {
    uint16_t pixel1 = (this->image_buffer_[1] << 8) | this->image_buffer_[0];
    uint16_t pixel2 = (this->image_buffer_[3] << 8) | this->image_buffer_[2];
    uint16_t pixel3 = (this->image_buffer_[5] << 8) | this->image_buffer_[4];
    uint16_t pixel4 = (this->image_buffer_[7] << 8) | this->image_buffer_[6];
    
    ESP_LOGD(TAG_IMAGE, "First 4 RGB565 pixels: 0x%04X 0x%04X 0x%04X 0x%04X", 
             pixel1, pixel2, pixel3, pixel4);
    
    // Décoder les couleurs pour verification
    uint8_t r1 = ((pixel1 >> 11) & 0x1F) << 3;
    uint8_t g1 = ((pixel1 >> 5) & 0x3F) << 2;
    uint8_t b1 = (pixel1 & 0x1F) << 3;
    ESP_LOGD(TAG_IMAGE, "First pixel RGB: R=%d G=%d B=%d", r1, g1, b1);
  }
  
  return true;
}

// =====================================================
// JPEG Decoder Callback Implementation
// =====================================================

int SdImageComponent::jpeg_decode_callback(JPEGDRAW *pDraw) {
  // Get the current component instance
  if (!current_image_component) {
    ESP_LOGE(TAG_IMAGE, "No current image component in callback");
    return 0; // Stop decoding
  }
  
  SdImageComponent *component = current_image_component;
  
  // Validate draw parameters
  if (!pDraw || !pDraw->pPixels) {
    ESP_LOGE(TAG_IMAGE, "Invalid draw parameters in callback");
    return 0;
  }
  
  // Log progress occasionally
  static int callback_count = 0;
  if (++callback_count % 100 == 0) {
    ESP_LOGD(TAG_IMAGE, "JPEG callback: %d,%d %dx%d", 
             pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight);
  }
  
  // Process pixels - JPEGDEC provides RGB565 pixels directly
  uint16_t *pixels = (uint16_t *)pDraw->pPixels;
  
  for (int py = 0; py < pDraw->iHeight; py++) {
    for (int px = 0; px < pDraw->iWidth; px++) {
      int img_x = pDraw->x + px;
      int img_y = pDraw->y + py;
      
      // Apply resize scaling if needed
      if (component->resize_width_ > 0 && component->resize_height_ > 0) {
        int orig_width = component->jpeg_decoder_->getWidth();
        int orig_height = component->jpeg_decoder_->getHeight();
        
        if (orig_width > 0 && orig_height > 0) {
          img_x = (img_x * component->resize_width_) / orig_width;
          img_y = (img_y * component->resize_height_) / orig_height;
        }
      }
      
      // Bounds check
      if (img_x >= 0 && img_x < component->image_width_ && 
          img_y >= 0 && img_y < component->image_height_) {
        
        // Get RGB565 pixel
        uint16_t rgb565 = pixels[py * pDraw->iWidth + px];
        
        // Store directly as RGB565 in buffer
        size_t offset = (img_y * component->image_width_ + img_x) * 2;
        if (offset + 1 < component->image_buffer_.size()) {
          component->image_buffer_[offset] = rgb565 & 0xFF;
          component->image_buffer_[offset + 1] = (rgb565 >> 8) & 0xFF;
        }
      }
    }
    
    // Yield periodically to prevent watchdog timeout
    if (py % 16 == 0) {
      App.feed_wdt();
      yield();
    }
  }
  
  return 1; // Continue decoding
}

bool SdImageComponent::jpeg_decode_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  // Apply resize scaling if needed
  if (this->resize_width_ > 0 && this->resize_height_ > 0) {
    int orig_width = this->jpeg_decoder_->getWidth();
    int orig_height = this->jpeg_decoder_->getHeight();
    x = (x * this->resize_width_) / orig_width;
    y = (y * this->resize_height_) / orig_height;
  }
  
  // Bounds check
  if (x < 0 || x >= this->image_width_ || y < 0 || y >= this->image_height_) {
    return false;
  }
  
  this->set_pixel(x, y, r, g, b);
  return true;
}

// =====================================================
// Helper Methods
// =====================================================

bool SdImageComponent::allocate_image_buffer() {
  size_t buffer_size = this->get_buffer_size();
  
  if (buffer_size == 0 || buffer_size > 3 * 1024 * 1024) { // 3MB limit for ESP32P4
    ESP_LOGE(TAG_IMAGE, "Invalid buffer size: %zu bytes", buffer_size);
    return false;
  }
  
  this->image_buffer_.clear();
  this->image_buffer_.resize(buffer_size, 0);
  ESP_LOGD(TAG_IMAGE, "Allocated image buffer: %zu bytes", buffer_size);
  return true;
}

void SdImageComponent::set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  if (x < 0 || x >= this->image_width_ || y < 0 || y >= this->image_height_) {
    return;
  }
  
  size_t offset = (y * this->image_width_ + x) * this->get_pixel_size();
  
  if (offset + this->get_pixel_size() > this->image_buffer_.size()) {
    return;
  }
  
  switch (this->format_) {
    case ImageFormat::RGB565: {
      uint16_t rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
      this->image_buffer_[offset] = rgb565 & 0xFF;
      this->image_buffer_[offset + 1] = (rgb565 >> 8) & 0xFF;
      break;
    }
    case ImageFormat::RGB888:
      this->image_buffer_[offset] = r;
      this->image_buffer_[offset + 1] = g;
      this->image_buffer_[offset + 2] = b;
      break;
    case ImageFormat::RGBA:
      this->image_buffer_[offset] = r;
      this->image_buffer_[offset + 1] = g;
      this->image_buffer_[offset + 2] = b;
      this->image_buffer_[offset + 3] = a;
      break;
  }
}

size_t SdImageComponent::get_pixel_size() const {
  switch (this->format_) {
    case ImageFormat::RGB565: return 2;
    case ImageFormat::RGB888: return 3;
    case ImageFormat::RGBA: return 4;
    default: return 2;
  }
}

size_t SdImageComponent::get_buffer_size() const {
  return this->image_width_ * this->image_height_ * this->get_pixel_size();
}

std::string SdImageComponent::format_to_string() const {
  switch (this->format_) {
    case ImageFormat::RGB565: return "RGB565";
    case ImageFormat::RGB888: return "RGB888";
    case ImageFormat::RGBA: return "RGBA";
    default: return "Unknown";
  }
}

std::string SdImageComponent::get_debug_info() const {
  char buffer[256];
  snprintf(buffer, sizeof(buffer), 
    "SdImage[%s]: %dx%d, %s, loaded=%s, size=%zu bytes",
    this->file_path_.c_str(),
    this->image_width_, this->image_height_,
    this->format_to_string().c_str(),
    this->image_loaded_ ? "yes" : "no",
    this->image_buffer_.size()
  );
  return std::string(buffer);
}

void SdImageComponent::list_directory_contents(const std::string &dir_path) {
  ESP_LOGI(TAG_IMAGE, "Directory listing for: %s", dir_path.c_str());
  
  DIR *dir = opendir(dir_path.c_str());
  if (!dir) {
    ESP_LOGE(TAG_IMAGE, "Cannot open directory: %s (errno: %d)", dir_path.c_str(), errno);
    return;
  }
  
  struct dirent *entry;
  int file_count = 0;
  
  while ((entry = readdir(dir)) != nullptr) {
    std::string full_path = dir_path;
    if (full_path.back() != '/') full_path += "/";
    full_path += entry->d_name;
    
    struct stat st;
    if (stat(full_path.c_str(), &st) == 0) {
      if (S_ISREG(st.st_mode)) {
        ESP_LOGI(TAG_IMAGE, "  📄 %s (%ld bytes)", entry->d_name, (long)st.st_size);
        file_count++;
      } else if (S_ISDIR(st.st_mode)) {
        ESP_LOGI(TAG_IMAGE, "  📁 %s/", entry->d_name);
      }
    }
  }
  
  closedir(dir);
  ESP_LOGI(TAG_IMAGE, "Total files: %d", file_count);
}

bool SdImageComponent::extract_jpeg_dimensions(const std::vector<uint8_t> &data, int &width, int &height) const {
  for (size_t i = 0; i < data.size() - 10; i++) {
    if (data[i] == 0xFF) {
      uint8_t marker = data[i + 1];
      if (marker >= 0xC0 && marker <= 0xC3) {
        if (i + 9 < data.size()) {
          height = (data[i + 5] << 8) | data[i + 6];
          width = (data[i + 7] << 8) | data[i + 8];
          return true;
        }
      }
    }
  }
  return false;
}

// =====================================================
// GIF Decoder Implementation
// =====================================================

bool SdImageComponent::decode_gif_image(const std::vector<uint8_t> &gif_data) {
  ESP_LOGD(TAG_IMAGE, "Decoding GIF image (%zu bytes)", gif_data.size());

  if (gif_data.size() < 13) {
    ESP_LOGE(TAG_IMAGE, "GIF data too small");
    return false;
  }

  size_t pos = 6; // Skip "GIF87a" or "GIF89a" header

  // Read Logical Screen Descriptor (7 bytes)
  uint16_t gif_width = gif_data[pos] | (gif_data[pos + 1] << 8);
  uint16_t gif_height = gif_data[pos + 2] | (gif_data[pos + 3] << 8);
  uint8_t packed = gif_data[pos + 4];
  uint8_t bg_color_index = gif_data[pos + 5];
  pos += 7;

  bool has_global_palette = (packed & 0x80) != 0;
  uint8_t palette_bits = (packed & 0x07) + 1;
  size_t palette_size = 1 << palette_bits;

  ESP_LOGI(TAG_IMAGE, "GIF dimensions: %dx%d, global palette: %s, colors: %zu, bg_index: %d",
           gif_width, gif_height, has_global_palette ? "yes" : "no", palette_size, bg_color_index);

  if (gif_width == 0 || gif_height == 0 || gif_width > 2048 || gif_height > 2048) {
    ESP_LOGE(TAG_IMAGE, "Invalid GIF dimensions: %dx%d", gif_width, gif_height);
    return false;
  }

  // Read Global Color Table if present
  std::vector<uint8_t> global_palette;
  if (has_global_palette) {
    size_t palette_bytes = palette_size * 3;
    if (pos + palette_bytes > gif_data.size()) {
      ESP_LOGE(TAG_IMAGE, "GIF data truncated (palette)");
      return false;
    }
    global_palette.assign(gif_data.begin() + pos, gif_data.begin() + pos + palette_bytes);
    pos += palette_bytes;
  }

  // Clear any existing animation frames
  this->gif_frames_.clear();
  this->current_gif_frame_ = 0;
  this->is_gif_animated_ = false;

  // Set image dimensions (original GIF size - we'll resize at the end if needed)
  uint16_t output_width = gif_width;
  uint16_t output_height = gif_height;
  bool needs_resize = (this->resize_width_ > 0 && this->resize_height_ > 0);

  if (needs_resize) {
    this->image_width_ = this->resize_width_;
    this->image_height_ = this->resize_height_;
    ESP_LOGI(TAG_IMAGE, "Will resize from %dx%d to %dx%d", gif_width, gif_height, this->image_width_, this->image_height_);
  } else {
    this->image_width_ = gif_width;
    this->image_height_ = gif_height;
  }

  // Composite buffer - holds the current composed image (at original GIF size)
  // This is what would be displayed at any moment
  std::vector<uint8_t> composite(gif_width * gif_height);  // Palette indices
  std::vector<bool> composite_set(gif_width * gif_height, false);  // Track which pixels are set

  // Initialize composite with background color
  std::fill(composite.begin(), composite.end(), bg_color_index);

  // Previous composite for disposal method 3
  std::vector<uint8_t> prev_composite = composite;

  // Frame parsing state
  struct FrameData {
    std::vector<uint8_t> compressed_data;
    std::vector<uint8_t> local_palette;
    uint16_t left, top, width, height;
    uint8_t lzw_min_code_size;
    bool has_transparency;
    uint8_t transparent_index;
    uint16_t delay_ms;
    uint8_t disposal_method;
  };

  std::vector<FrameData> frames;
  FrameData current_frame;
  current_frame.delay_ms = 100;
  current_frame.disposal_method = 0;
  current_frame.has_transparency = false;
  current_frame.transparent_index = 0;

  // Parse all blocks and collect all frames
  while (pos < gif_data.size()) {
    uint8_t separator = gif_data[pos++];

    if (separator == 0x21) {  // Extension block
      if (pos >= gif_data.size()) break;
      uint8_t label = gif_data[pos++];

      if (label == 0xF9) {  // Graphic Control Extension
        if (pos + 6 > gif_data.size()) break;
        pos++; // skip block size
        uint8_t packed_gce = gif_data[pos++];
        uint16_t delay = gif_data[pos] | (gif_data[pos + 1] << 8);
        pos += 2;
        current_frame.transparent_index = gif_data[pos++];
        current_frame.has_transparency = (packed_gce & 0x01) != 0;
        current_frame.disposal_method = (packed_gce >> 2) & 0x07;
        current_frame.delay_ms = delay * 10;
        if (current_frame.delay_ms == 0) current_frame.delay_ms = 100;
        pos++; // Block terminator
        ESP_LOGD(TAG_IMAGE, "GCE: delay=%dms, transparency=%s, index=%d, disposal=%d",
                 current_frame.delay_ms, current_frame.has_transparency ? "yes" : "no",
                 current_frame.transparent_index, current_frame.disposal_method);
      } else {
        while (pos < gif_data.size()) {
          uint8_t block_size = gif_data[pos++];
          if (block_size == 0) break;
          pos += block_size;
        }
      }
    } else if (separator == 0x2C) {  // Image descriptor
      if (pos + 9 > gif_data.size()) break;

      current_frame.left = gif_data[pos] | (gif_data[pos + 1] << 8);
      current_frame.top = gif_data[pos + 2] | (gif_data[pos + 3] << 8);
      current_frame.width = gif_data[pos + 4] | (gif_data[pos + 5] << 8);
      current_frame.height = gif_data[pos + 6] | (gif_data[pos + 7] << 8);
      uint8_t packed_img = gif_data[pos + 8];
      pos += 9;

      bool has_local_palette = (packed_img & 0x80) != 0;
      uint8_t local_palette_bits = (packed_img & 0x07) + 1;
      size_t local_palette_size = 1 << local_palette_bits;

      ESP_LOGI(TAG_IMAGE, "Frame %zu: %dx%d at (%d,%d), disposal=%d, transparency=%s",
               frames.size(), current_frame.width, current_frame.height,
               current_frame.left, current_frame.top, current_frame.disposal_method,
               current_frame.has_transparency ? "yes" : "no");

      current_frame.local_palette.clear();
      if (has_local_palette) {
        size_t local_palette_bytes = local_palette_size * 3;
        if (pos + local_palette_bytes > gif_data.size()) {
          ESP_LOGE(TAG_IMAGE, "GIF data truncated (local palette)");
          return false;
        }
        current_frame.local_palette.assign(gif_data.begin() + pos, gif_data.begin() + pos + local_palette_bytes);
        pos += local_palette_bytes;
      }

      if (pos >= gif_data.size()) break;
      current_frame.lzw_min_code_size = gif_data[pos++];

      if (current_frame.lzw_min_code_size < 2 || current_frame.lzw_min_code_size > 11) {
        ESP_LOGE(TAG_IMAGE, "Invalid LZW code size: %d", current_frame.lzw_min_code_size);
        return false;
      }

      current_frame.compressed_data.clear();
      while (pos < gif_data.size()) {
        uint8_t block_size = gif_data[pos++];
        if (block_size == 0) break;
        if (pos + block_size > gif_data.size()) {
          ESP_LOGE(TAG_IMAGE, "Compressed data block overflow");
          return false;
        }
        current_frame.compressed_data.insert(current_frame.compressed_data.end(),
                                              gif_data.begin() + pos,
                                              gif_data.begin() + pos + block_size);
        pos += block_size;
      }

      frames.push_back(current_frame);

      // Check both frame count and estimated memory usage
      size_t frame_size = this->image_width_ * this->image_height_ * 3;  // pixels (2B) + transparency (1B)
      size_t estimated_memory = frames.size() * frame_size;
      size_t max_memory = MAX_GIF_MEMORY_MB * 1024 * 1024;

      if (frames.size() >= MAX_GIF_FRAMES) {
        ESP_LOGW(TAG_IMAGE, "Reached max frame limit (%zu frames)", MAX_GIF_FRAMES);
        break;
      }
      if (estimated_memory > max_memory) {
        ESP_LOGW(TAG_IMAGE, "Reached memory limit (%zu frames, ~%zu KB)", frames.size(), estimated_memory / 1024);
        break;
      }

      current_frame = FrameData();
      current_frame.delay_ms = 100;
      current_frame.disposal_method = 0;
      current_frame.has_transparency = false;
      current_frame.transparent_index = 0;

    } else if (separator == 0x3B) {
      break;
    } else {
      ESP_LOGW(TAG_IMAGE, "Unknown GIF separator: 0x%02X at pos %zu", separator, pos - 1);
      break;
    }
  }

  if (frames.empty()) {
    ESP_LOGE(TAG_IMAGE, "No image data found in GIF");
    return false;
  }

  ESP_LOGI(TAG_IMAGE, "Found %zu frame(s) in GIF", frames.size());
  this->is_gif_animated_ = (frames.size() > 1);

  // Allocate output buffer
  if (!this->allocate_image_buffer()) {
    ESP_LOGE(TAG_IMAGE, "Failed to allocate image buffer");
    return false;
  }

  // Process each frame and create composited frames
  for (size_t frame_idx = 0; frame_idx < frames.size(); frame_idx++) {
    const FrameData &frame = frames[frame_idx];

    ESP_LOGD(TAG_IMAGE, "Processing frame %zu/%zu...", frame_idx + 1, frames.size());

    // Choose palette
    const std::vector<uint8_t> *palette_ptr = nullptr;
    if (!frame.local_palette.empty()) {
      palette_ptr = &frame.local_palette;
    } else if (!global_palette.empty()) {
      palette_ptr = &global_palette;
    } else {
      ESP_LOGE(TAG_IMAGE, "No palette for frame %zu", frame_idx);
      return false;
    }
    const std::vector<uint8_t> &palette = *palette_ptr;

    // LZW Decompression
    const uint16_t clear_code = 1 << frame.lzw_min_code_size;
    const uint16_t end_code = clear_code + 1;
    uint16_t next_code = end_code + 1;
    uint16_t code_size = frame.lzw_min_code_size + 1;
    uint16_t code_mask = (1 << code_size) - 1;

    std::vector<std::vector<uint8_t>> table(4096);
    for (uint16_t i = 0; i < clear_code; i++) {
      table[i] = {static_cast<uint8_t>(i)};
    }

    std::vector<uint8_t> decoded_indices;
    decoded_indices.reserve(frame.width * frame.height);

    size_t bit_pos = 0;
    std::vector<uint8_t> prev_sequence;

    auto read_code = [&]() -> uint16_t {
      uint32_t code = 0;
      for (int i = 0; i < code_size; i++) {
        size_t byte_pos = bit_pos / 8;
        size_t bit_offset = bit_pos % 8;
        if (byte_pos >= frame.compressed_data.size()) return end_code;
        code |= ((frame.compressed_data[byte_pos] >> bit_offset) & 1) << i;
        bit_pos++;
      }
      return static_cast<uint16_t>(code);
    };

    while (bit_pos < frame.compressed_data.size() * 8) {
      uint16_t code = read_code();

      if (code == end_code) break;

      if (code == clear_code) {
        next_code = end_code + 1;
        code_size = frame.lzw_min_code_size + 1;
        code_mask = (1 << code_size) - 1;
        prev_sequence.clear();
        for (size_t i = end_code + 1; i < table.size(); i++) {
          table[i].clear();
        }
        continue;
      }

      std::vector<uint8_t> sequence;
      if (code < next_code) {
        sequence = table[code];
      } else if (code == next_code) {
        sequence = prev_sequence;
        if (!prev_sequence.empty()) sequence.push_back(prev_sequence[0]);
      } else {
        ESP_LOGE(TAG_IMAGE, "Invalid LZW code: %d", code);
        return false;
      }

      decoded_indices.insert(decoded_indices.end(), sequence.begin(), sequence.end());

      if (!prev_sequence.empty() && next_code < 4096) {
        std::vector<uint8_t> new_entry = prev_sequence;
        new_entry.push_back(sequence[0]);
        table[next_code] = new_entry;
        next_code++;
        if (next_code > code_mask && code_size < 12) {
          code_size++;
          code_mask = (1 << code_size) - 1;
        }
      }
      prev_sequence = sequence;

      if (decoded_indices.size() % 2048 == 0) {
        App.feed_wdt();
        yield();
      }
    }

    // Save previous composite for disposal method 3
    if (frame.disposal_method == 3) {
      prev_composite = composite;
    }

    // Apply frame to composite at position (left, top)
    for (int fy = 0; fy < frame.height; fy++) {
      for (int fx = 0; fx < frame.width; fx++) {
        int gx = frame.left + fx;
        int gy = frame.top + fy;

        if (gx >= gif_width || gy >= gif_height) continue;

        size_t src_idx = fy * frame.width + fx;
        size_t dst_idx = gy * gif_width + gx;

        if (src_idx >= decoded_indices.size()) continue;

        uint8_t idx = decoded_indices[src_idx];

        // Skip transparent pixels
        if (frame.has_transparency && idx == frame.transparent_index) {
          continue;
        }

        composite[dst_idx] = idx;
        composite_set[dst_idx] = true;
      }
    }

    // Create GifFrame from current composite
    GifFrame gif_frame;
    gif_frame.pixels.resize(this->image_width_ * this->image_height_ * 2);
    gif_frame.transparency.resize(this->image_width_ * this->image_height_, false);
    gif_frame.delay_ms = frame.delay_ms;
    gif_frame.left = 0;
    gif_frame.top = 0;
    gif_frame.width = this->image_width_;
    gif_frame.height = this->image_height_;
    gif_frame.disposal_method = frame.disposal_method;
    gif_frame.has_transparency = false;

    // Convert composite to RGB565 (with optional resize)
    for (int y = 0; y < this->image_height_; y++) {
      for (int x = 0; x < this->image_width_; x++) {
        // Map to source coordinates (for resize)
        int src_x = needs_resize ? (x * gif_width / this->image_width_) : x;
        int src_y = needs_resize ? (y * gif_height / this->image_height_) : y;

        size_t src_idx = src_y * gif_width + src_x;
        size_t dst_idx = y * this->image_width_ + x;

        uint8_t palette_idx = composite[src_idx];
        bool is_set = composite_set[src_idx];

        uint8_t r = 0, g = 0, b = 0;

        if (is_set && palette_idx * 3 + 2 < palette.size()) {
          r = palette[palette_idx * 3];
          g = palette[palette_idx * 3 + 1];
          b = palette[palette_idx * 3 + 2];
        } else if (bg_color_index * 3 + 2 < palette.size()) {
          // Use background color
          r = palette[bg_color_index * 3];
          g = palette[bg_color_index * 3 + 1];
          b = palette[bg_color_index * 3 + 2];
          gif_frame.transparency[dst_idx] = true;
          gif_frame.has_transparency = true;
        }

        uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        size_t pixel_pos = dst_idx * 2;
        gif_frame.pixels[pixel_pos] = rgb565 & 0xFF;
        gif_frame.pixels[pixel_pos + 1] = (rgb565 >> 8) & 0xFF;
      }

      if (y % 32 == 0) {
        App.feed_wdt();
        yield();
      }
    }

    this->gif_frames_.push_back(std::move(gif_frame));

    // Handle disposal method for next frame
    switch (frame.disposal_method) {
      case 2:  // Restore to background
        for (int fy = 0; fy < frame.height; fy++) {
          for (int fx = 0; fx < frame.width; fx++) {
            int gx = frame.left + fx;
            int gy = frame.top + fy;
            if (gx < gif_width && gy < gif_height) {
              size_t idx = gy * gif_width + gx;
              composite[idx] = bg_color_index;
              composite_set[idx] = false;
            }
          }
        }
        break;
      case 3:  // Restore to previous
        composite = prev_composite;
        break;
      // Case 0, 1: Do not dispose - keep composite as-is
    }

    App.feed_wdt();
  }

  // Copy first frame to image_buffer_
  if (!this->gif_frames_.empty()) {
    memcpy(this->image_buffer_.data(), this->gif_frames_[0].pixels.data(), this->image_buffer_.size());
    ESP_LOGI(TAG_IMAGE, "GIF decoded successfully: %zu frame(s), animated: %s",
             this->gif_frames_.size(), this->is_gif_animated_ ? "yes" : "no");
  }

  return true;
}

// =====================================================
// PNG Decoder Implementation (via LVGL)
// =====================================================

bool SdImageComponent::decode_png_image(const std::vector<uint8_t> &png_data) {
#ifdef LV_USE_LIBPNG
  ESP_LOGD(TAG_IMAGE, "Using LVGL PNG decoder (LV_USE_LIBPNG)");

  // For PNG, we need to use LVGL's decoder
  // The strategy is to store raw data and let LVGL decode it when needed
  // For now, we'll use a simple approach: decode via LVGL decoder manually

  // Note: This requires LVGL png decoder to be active
  // User must have lvgl_advanced_features with libpng: true

  ESP_LOGW(TAG_IMAGE, "PNG decoding via LVGL decoder - requires direct LVGL integration");
  ESP_LOGW(TAG_IMAGE, "Consider using LVGL's img widget directly with file path");

  // For compatibility, return error for now - user should use LVGL img widget
  ESP_LOGE(TAG_IMAGE, "PNG decoding not yet implemented in storage component");
  ESP_LOGI(TAG_IMAGE, "Workaround: Use LVGL's lv_img widget with file:// path to let LVGL decode PNG");

  return false;
#else
  ESP_LOGE(TAG_IMAGE, "PNG decoder not available - enable lvgl_advanced_features with libpng: true");
  return false;
#endif
}

// =====================================================
// BMP Decoder Implementation (via LVGL)
// =====================================================

bool SdImageComponent::decode_bmp_image(const std::vector<uint8_t> &bmp_data) {
#ifdef LV_USE_BMP
  ESP_LOGD(TAG_IMAGE, "Using LVGL BMP decoder (LV_USE_BMP)");

  ESP_LOGW(TAG_IMAGE, "BMP decoding via LVGL decoder - requires direct LVGL integration");
  ESP_LOGW(TAG_IMAGE, "Consider using LVGL's img widget directly with file path");

  ESP_LOGE(TAG_IMAGE, "BMP decoding not yet implemented in storage component");
  ESP_LOGI(TAG_IMAGE, "Workaround: Use LVGL's lv_img widget with file:// path to let LVGL decode BMP");

  return false;
#else
  ESP_LOGE(TAG_IMAGE, "BMP decoder not available - enable lvgl_advanced_features with bmp: true");
  return false;
#endif
}

// =====================================================
// SVG Decoder Implementation (via LVGL + ThorVG)
// =====================================================

bool SdImageComponent::decode_svg_image(const std::vector<uint8_t> &svg_data) {
#ifdef LV_USE_SVG
  ESP_LOGD(TAG_IMAGE, "Using LVGL SVG decoder (LV_USE_SVG + ThorVG)");

  ESP_LOGW(TAG_IMAGE, "SVG decoding via LVGL + ThorVG - requires direct LVGL integration");
  ESP_LOGW(TAG_IMAGE, "SVG is a vector format - best used with LVGL's img widget directly");

  ESP_LOGE(TAG_IMAGE, "SVG decoding not yet implemented in storage component");
  ESP_LOGI(TAG_IMAGE, "Workaround: Use LVGL's lv_img widget with file:// path to let LVGL decode SVG");
  ESP_LOGI(TAG_IMAGE, "Requires: lvgl_advanced_features with svg: true and thorvg: {internal: true}");

  return false;
#else
  ESP_LOGE(TAG_IMAGE, "SVG decoder not available - enable lvgl_advanced_features with svg: true (LVGL v9 only)");
  return false;
#endif
}

// =====================================================
// Lottie Decoder Implementation (via LVGL + ThorVG)
// =====================================================

bool SdImageComponent::decode_lottie_image(const std::vector<uint8_t> &lottie_data) {
#ifdef LV_USE_LOTTIE
  ESP_LOGD(TAG_IMAGE, "Using LVGL Lottie decoder (LV_USE_LOTTIE + ThorVG)");

  ESP_LOGW(TAG_IMAGE, "Lottie decoding via LVGL + ThorVG - requires direct LVGL integration");
  ESP_LOGW(TAG_IMAGE, "Lottie is an animation format - best used with LVGL's lottie widget directly");

  ESP_LOGE(TAG_IMAGE, "Lottie decoding not yet implemented in storage component");
  ESP_LOGI(TAG_IMAGE, "Workaround: Use LVGL's lv_lottie widget with file:// path");
  ESP_LOGI(TAG_IMAGE, "Requires: lvgl_advanced_features with lottie: true and thorvg: {internal: true}");

  return false;
#else
  ESP_LOGE(TAG_IMAGE, "Lottie decoder not available - enable lvgl_advanced_features with lottie: true (LVGL v9 only)");
  return false;
#endif
}

}  // namespace storage
}  // namespace esphome











