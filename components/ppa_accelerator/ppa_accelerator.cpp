#include "ppa_accelerator.h"

#ifdef CONFIG_IDF_TARGET_ESP32P4

#include "esp_timer.h"

namespace esphome {
namespace ppa_accelerator {

// Global instance
static PPAAccelerator* global_ppa_instance = nullptr;

PPAAccelerator* get_ppa_accelerator() {
  return global_ppa_instance;
}

PPAAccelerator::~PPAAccelerator() {
  if (this->srm_client_) {
    ppa_unregister_client(this->srm_client_);
  }
  if (this->blend_client_) {
    ppa_unregister_client(this->blend_client_);
  }
  if (this->fill_client_) {
    ppa_unregister_client(this->fill_client_);
  }
  if (global_ppa_instance == this) {
    global_ppa_instance = nullptr;
  }
}

void PPAAccelerator::setup() {
  ESP_LOGI(TAG, "Initializing PPA Accelerator for ESP32-P4...");

  esp_err_t ret;

  // Register SRM (Scale-Rotate-Mirror) client
  ppa_client_config_t srm_config = {
    .oper_type = PPA_OPERATION_SRM,
    .max_pending_trans_num = 1,
  };
  ret = ppa_register_client(&srm_config, &this->srm_client_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register SRM client: %s", esp_err_to_name(ret));
    return;
  }
  ESP_LOGD(TAG, "SRM client registered");

  // Register Blend client
  ppa_client_config_t blend_config = {
    .oper_type = PPA_OPERATION_BLEND,
    .max_pending_trans_num = 1,
  };
  ret = ppa_register_client(&blend_config, &this->blend_client_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register Blend client: %s", esp_err_to_name(ret));
    ppa_unregister_client(this->srm_client_);
    this->srm_client_ = nullptr;
    return;
  }
  ESP_LOGD(TAG, "Blend client registered");

  // Register Fill client
  ppa_client_config_t fill_config = {
    .oper_type = PPA_OPERATION_FILL,
    .max_pending_trans_num = 1,
  };
  ret = ppa_register_client(&fill_config, &this->fill_client_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register Fill client: %s", esp_err_to_name(ret));
    ppa_unregister_client(this->srm_client_);
    ppa_unregister_client(this->blend_client_);
    this->srm_client_ = nullptr;
    this->blend_client_ = nullptr;
    return;
  }
  ESP_LOGD(TAG, "Fill client registered");

  this->initialized_ = true;
  global_ppa_instance = this;
  ESP_LOGI(TAG, "PPA Accelerator initialized successfully!");
}

void PPAAccelerator::dump_config() {
  ESP_LOGCONFIG(TAG, "PPA Accelerator (ESP32-P4):");
  ESP_LOGCONFIG(TAG, "  Status: %s", this->initialized_ ? "READY" : "FAILED");
  if (this->initialized_) {
    ESP_LOGCONFIG(TAG, "  SRM Client: %s", this->srm_client_ ? "OK" : "NONE");
    ESP_LOGCONFIG(TAG, "  Blend Client: %s", this->blend_client_ ? "OK" : "NONE");
    ESP_LOGCONFIG(TAG, "  Fill Client: %s", this->fill_client_ ? "OK" : "NONE");
    ESP_LOGCONFIG(TAG, "  Supported formats: RGB565, RGB888, ARGB8888");
    ESP_LOGCONFIG(TAG, "  Operations: Scale, Rotate, Mirror, Blend, Fill");
  }
}

// ============================================================================
// Format conversion helpers
// ============================================================================

ppa_srm_color_mode_t PPAAccelerator::to_ppa_srm_format(PixelFormat format) const {
  switch (format) {
    case PixelFormat::RGB565: return PPA_SRM_COLOR_MODE_RGB565;
    case PixelFormat::RGB888: return PPA_SRM_COLOR_MODE_RGB888;
    case PixelFormat::ARGB8888: return PPA_SRM_COLOR_MODE_ARGB8888;
    default: return PPA_SRM_COLOR_MODE_RGB565;
  }
}

ppa_blend_color_mode_t PPAAccelerator::to_ppa_blend_format(PixelFormat format) const {
  switch (format) {
    case PixelFormat::RGB565: return PPA_BLEND_COLOR_MODE_RGB565;
    case PixelFormat::RGB888: return PPA_BLEND_COLOR_MODE_RGB888;
    case PixelFormat::ARGB8888: return PPA_BLEND_COLOR_MODE_ARGB8888;
    default: return PPA_BLEND_COLOR_MODE_RGB565;
  }
}

ppa_fill_color_mode_t PPAAccelerator::to_ppa_fill_format(PixelFormat format) const {
  switch (format) {
    case PixelFormat::RGB565: return PPA_FILL_COLOR_MODE_RGB565;
    case PixelFormat::RGB888: return PPA_FILL_COLOR_MODE_RGB888;
    case PixelFormat::ARGB8888: return PPA_FILL_COLOR_MODE_ARGB8888;
    default: return PPA_FILL_COLOR_MODE_RGB565;
  }
}

// ============================================================================
// Buffer management
// ============================================================================

uint8_t* PPAAccelerator::allocate_dma_buffer(size_t size) {
  // PPA requires DMA-capable memory with specific alignment
  // Use MALLOC_CAP_DMA for DMA-capable memory
  uint8_t* buffer = (uint8_t*)heap_caps_aligned_alloc(64, size, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
  if (!buffer) {
    // Fallback to internal DMA memory
    buffer = (uint8_t*)heap_caps_aligned_alloc(64, size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  }
  if (!buffer) {
    ESP_LOGE(TAG, "Failed to allocate DMA buffer of %zu bytes", size);
  }
  return buffer;
}

void PPAAccelerator::free_dma_buffer(uint8_t* buffer) {
  if (buffer) {
    heap_caps_free(buffer);
  }
}

bool PPAAccelerator::is_dma_capable(const uint8_t* buffer) {
  return heap_caps_check_integrity_addr((intptr_t)buffer, true);
}

// ============================================================================
// Scale operations
// ============================================================================

PPAResult PPAAccelerator::scale(const PPABuffer& input, PPABuffer& output,
                                 float scale_x, float scale_y) {
  return do_srm_operation(input, output, scale_x, scale_y, 0, false, false);
}

PPAResult PPAAccelerator::scale_to_size(const PPABuffer& input, PPABuffer& output,
                                         uint16_t target_width, uint16_t target_height) {
  if (input.width == 0 || input.height == 0) {
    return {false, "Invalid input dimensions", 0};
  }

  float scale_x = (float)target_width / input.width;
  float scale_y = (float)target_height / input.height;

  // Update output dimensions
  output.width = target_width;
  output.height = target_height;

  return do_srm_operation(input, output, scale_x, scale_y, 0, false, false);
}

// ============================================================================
// Mirror/Rotate operations
// ============================================================================

PPAResult PPAAccelerator::mirror_horizontal(const PPABuffer& input, PPABuffer& output) {
  return do_srm_operation(input, output, 1.0f, 1.0f, 0, true, false);
}

PPAResult PPAAccelerator::mirror_vertical(const PPABuffer& input, PPABuffer& output) {
  return do_srm_operation(input, output, 1.0f, 1.0f, 0, false, true);
}

PPAResult PPAAccelerator::rotate(const PPABuffer& input, PPABuffer& output, int angle_degrees) {
  // PPA only supports 0, 90, 180, 270 degrees
  int normalized_angle = ((angle_degrees % 360) + 360) % 360;
  if (normalized_angle != 0 && normalized_angle != 90 &&
      normalized_angle != 180 && normalized_angle != 270) {
    return {false, "PPA only supports 0, 90, 180, 270 degree rotation", 0};
  }
  return do_srm_operation(input, output, 1.0f, 1.0f, normalized_angle, false, false);
}

// ============================================================================
// Core SRM operation
// ============================================================================

PPAResult PPAAccelerator::do_srm_operation(const PPABuffer& input, PPABuffer& output,
                                            float scale_x, float scale_y,
                                            int rotation_angle, bool mirror_x, bool mirror_y) {
  PPAResult result = {false, nullptr, 0};

  if (!this->initialized_ || !this->srm_client_) {
    result.error_message = "PPA not initialized";
    return result;
  }

  if (!input.data || !output.data) {
    result.error_message = "Null buffer pointer";
    return result;
  }

  if (input.data == output.data) {
    result.error_message = "Input and output buffers must be different for SRM";
    return result;
  }

  uint64_t start_time = esp_timer_get_time();

  // Configure input picture block
  ppa_in_pic_blk_config_t in_config = {};
  in_config.buffer = input.data;
  in_config.pic_w = input.width;
  in_config.pic_h = input.height;
  in_config.block_w = input.width;
  in_config.block_h = input.height;
  in_config.block_offset_x = 0;
  in_config.block_offset_y = 0;
  in_config.srm_cm = to_ppa_srm_format(input.format);

  // Configure output picture block
  ppa_out_pic_blk_config_t out_config = {};
  out_config.buffer = output.data;
  out_config.buffer_size = output.get_size();
  out_config.pic_w = output.width;
  out_config.pic_h = output.height;
  out_config.block_offset_x = 0;
  out_config.block_offset_y = 0;
  out_config.srm_cm = to_ppa_srm_format(output.format);

  // Configure SRM operation
  ppa_srm_oper_config_t srm_config = {};
  srm_config.in = in_config;
  srm_config.out = out_config;
  srm_config.rotation_angle = static_cast<ppa_srm_rotation_angle_t>(rotation_angle / 90);
  srm_config.scale_x = scale_x;
  srm_config.scale_y = scale_y;
  srm_config.mirror_x = mirror_x;
  srm_config.mirror_y = mirror_y;
  srm_config.rgb_swap = 0;
  srm_config.byte_swap = 0;
  srm_config.mode = PPA_TRANS_MODE_BLOCKING;

  // Execute SRM operation
  esp_err_t ret = ppa_do_scale_rotate_mirror(this->srm_client_, &srm_config);

  uint64_t end_time = esp_timer_get_time();
  result.processing_time_us = (uint32_t)(end_time - start_time);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SRM operation failed: %s", esp_err_to_name(ret));
    result.error_message = esp_err_to_name(ret);
    return result;
  }

  result.success = true;
  ESP_LOGD(TAG, "SRM completed in %lu us (%.1fx%.1f scale)",
           result.processing_time_us, scale_x, scale_y);

  return result;
}

// ============================================================================
// Blend operation
// ============================================================================

PPAResult PPAAccelerator::blend(const PPABuffer& foreground, const PPABuffer& background,
                                 PPABuffer& output, uint8_t fg_alpha) {
  PPAResult result = {false, nullptr, 0};

  if (!this->initialized_ || !this->blend_client_) {
    result.error_message = "PPA not initialized";
    return result;
  }

  if (!foreground.data || !background.data || !output.data) {
    result.error_message = "Null buffer pointer";
    return result;
  }

  // FG and BG must have same dimensions for blend
  if (foreground.width != background.width || foreground.height != background.height) {
    result.error_message = "FG and BG dimensions must match";
    return result;
  }

  uint64_t start_time = esp_timer_get_time();

  // Configure foreground
  ppa_in_pic_blk_config_t fg_config = {};
  fg_config.buffer = foreground.data;
  fg_config.pic_w = foreground.width;
  fg_config.pic_h = foreground.height;
  fg_config.block_w = foreground.width;
  fg_config.block_h = foreground.height;
  fg_config.block_offset_x = 0;
  fg_config.block_offset_y = 0;
  fg_config.blend_cm = to_ppa_blend_format(foreground.format);

  // Configure background
  ppa_in_pic_blk_config_t bg_config = {};
  bg_config.buffer = background.data;
  bg_config.pic_w = background.width;
  bg_config.pic_h = background.height;
  bg_config.block_w = background.width;
  bg_config.block_h = background.height;
  bg_config.block_offset_x = 0;
  bg_config.block_offset_y = 0;
  bg_config.blend_cm = to_ppa_blend_format(background.format);

  // Configure output
  ppa_out_pic_blk_config_t out_config = {};
  out_config.buffer = output.data;
  out_config.buffer_size = output.get_size();
  out_config.pic_w = output.width;
  out_config.pic_h = output.height;
  out_config.block_offset_x = 0;
  out_config.block_offset_y = 0;
  out_config.blend_cm = to_ppa_blend_format(output.format);

  // Configure blend operation
  ppa_blend_oper_config_t blend_config = {};
  blend_config.in_fg = fg_config;
  blend_config.in_bg = bg_config;
  blend_config.out = out_config;
  blend_config.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
  blend_config.fg_alpha_fix_val = fg_alpha;
  blend_config.bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
  blend_config.bg_alpha_fix_val = 255;
  blend_config.mode = PPA_TRANS_MODE_BLOCKING;

  // Execute blend operation
  esp_err_t ret = ppa_do_blend(this->blend_client_, &blend_config);

  uint64_t end_time = esp_timer_get_time();
  result.processing_time_us = (uint32_t)(end_time - start_time);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Blend operation failed: %s", esp_err_to_name(ret));
    result.error_message = esp_err_to_name(ret);
    return result;
  }

  result.success = true;
  ESP_LOGD(TAG, "Blend completed in %lu us", result.processing_time_us);

  return result;
}

// ============================================================================
// Fill operations
// ============================================================================

PPAResult PPAAccelerator::fill(PPABuffer& buffer, uint32_t color_argb8888) {
  return fill_rect(buffer, 0, 0, buffer.width, buffer.height, color_argb8888);
}

PPAResult PPAAccelerator::fill_rect(PPABuffer& buffer, uint16_t x, uint16_t y,
                                     uint16_t width, uint16_t height, uint32_t color_argb8888) {
  PPAResult result = {false, nullptr, 0};

  if (!this->initialized_ || !this->fill_client_) {
    result.error_message = "PPA not initialized";
    return result;
  }

  if (!buffer.data) {
    result.error_message = "Null buffer pointer";
    return result;
  }

  // Bounds check
  if (x + width > buffer.width || y + height > buffer.height) {
    result.error_message = "Fill region exceeds buffer bounds";
    return result;
  }

  uint64_t start_time = esp_timer_get_time();

  // Configure output
  ppa_out_pic_blk_config_t out_config = {};
  out_config.buffer = buffer.data;
  out_config.buffer_size = buffer.get_size();
  out_config.pic_w = buffer.width;
  out_config.pic_h = buffer.height;
  out_config.block_offset_x = x;
  out_config.block_offset_y = y;
  out_config.fill_cm = to_ppa_fill_format(buffer.format);

  // Configure fill operation
  ppa_fill_oper_config_t fill_config = {};
  fill_config.out = out_config;
  fill_config.fill_block_w = width;
  fill_config.fill_block_h = height;
  // fill_argb_color is a color_pixel_argb8888_data_t struct
  fill_config.fill_argb_color.val = color_argb8888;
  fill_config.mode = PPA_TRANS_MODE_BLOCKING;

  // Execute fill operation
  esp_err_t ret = ppa_do_fill(this->fill_client_, &fill_config);

  uint64_t end_time = esp_timer_get_time();
  result.processing_time_us = (uint32_t)(end_time - start_time);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Fill operation failed: %s", esp_err_to_name(ret));
    result.error_message = esp_err_to_name(ret);
    return result;
  }

  result.success = true;
  ESP_LOGD(TAG, "Fill completed in %lu us (%dx%d)", result.processing_time_us, width, height);

  return result;
}

}  // namespace ppa_accelerator
}  // namespace esphome

#endif  // CONFIG_IDF_TARGET_ESP32P4
