#include "yolov11_component.h"
#include "esphome/core/log.h"

#ifdef USE_YOLOV11_ESP32_CAMERA
#include "esphome/components/esp32_camera/esp32_camera.h"
#endif

#if defined(USE_ESP_IDF) && defined(USE_YOLOV11_MIPI_CAMERA)
#include "esp_cache.h"
#endif

namespace esphome {
namespace yolov11 {

static const char *const TAG = "yolov11";

// 5x7 bitmap font for drawing text on RGB565 frame buffer
static const uint8_t FONT_5X7[][7] = {
  // A-Z (index 0-25)
  {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // A
  {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, // B
  {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, // C
  {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}, // D
  {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, // E
  {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, // F
  {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, // G
  {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // H
  {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, // I
  {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, // J
  {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, // K
  {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, // L
  {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, // M
  {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}, // N
  {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // O
  {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, // P
  {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, // Q
  {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, // R
  {0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E}, // S
  {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, // T
  {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // U
  {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, // V
  {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, // W
  {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, // X
  {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, // Y
  {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, // Z
  // 0-9 (index 26-35)
  {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, // 0
  {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 1
  {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, // 2
  {0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E}, // 3
  {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 4
  {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, // 5
  {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 6
  {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
  {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 8
  {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, // 9
  // Special characters (index 36-40)
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // Space
  {0x00, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00}, // : (colon)
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08}, // , (comma)
  {0x11, 0x11, 0x09, 0x01, 0x12, 0x12, 0x0C}, // % (percent)
  {0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00}, // . (dot)
  {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}, // - (hyphen, index 41)
};

void YOLOV11Component::setup() {
  ESP_LOGCONFIG(TAG, "Setting up YOLOV11...");

  this->detections_mutex_ = xSemaphoreCreateMutex();
  if (this->detections_mutex_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create detections mutex");
    this->mark_failed();
    return;
  }

  if (this->model_file_ == nullptr) {
    ESP_LOGE(TAG, "Model file not configured");
    this->mark_failed();
    return;
  }

  // Initialize the detector
  this->init_detector_();

#ifdef USE_YOLOV11_ESP32_CAMERA
  if (this->esp32_camera_ != nullptr) {
    // Register image callback with ESP32 camera
    this->esp32_camera_->add_image_callback(
        [this](std::shared_ptr<esp32_camera::CameraImage> image) {
          this->on_esp32_camera_image_(std::move(image));
        });
    ESP_LOGI(TAG, "Registered with ESP32 camera");
  }
#endif

#ifdef USE_YOLOV11_MIPI_CAMERA
  if (this->mipi_camera_ != nullptr) {
    ESP_LOGI(TAG, "Registered with MIPI camera (esp_cam_sensor)");
  }
#endif

  ESP_LOGI(TAG, "YOLOV11 ready (score_thr=%.2f, nms_thr=%.2f)",
           this->score_threshold_, this->nms_threshold_);
}

void YOLOV11Component::init_detector_() {
#ifndef ESP_DL_MODEL_YOLO11
  ESP_LOGE(TAG, "ESP_DL_MODEL_YOLO11 not defined - cannot initialize detector");
  this->mark_failed();
  return;
#else
  const uint8_t *model_data = this->model_file_->get_data();
  size_t model_size = this->model_file_->get_size();

  if (model_data == nullptr || model_size == 0) {
    ESP_LOGE(TAG, "Model data is empty");
    this->mark_failed();
    return;
  }

  const char *model_type_str = (this->model_type_ == MODEL_TYPE_YOLO26N) ? "yolo26n" : "yolo11";
  ESP_LOGI(TAG, "Loading %s model (%u bytes)...", model_type_str, (unsigned)model_size);

  // ESP32-P4 ESP-DL requires MALLOC_CAP_SIMD memory (separate, smaller pool)
  // Query the actual SIMD-capable block size to avoid allocation failure + crash
  size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  size_t largest_simd = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
#ifdef USE_YOLOV11_MIPI_CAMERA
  // P4: check SIMD pool specifically (cap 0x800)
  size_t simd_block = heap_caps_get_largest_free_block(0x800);  // MALLOC_CAP_SIMD
  if (simd_block > 0 && simd_block < largest_simd) {
    largest_simd = simd_block;
  }
#endif
  size_t max_internal = (largest_simd > 16 * 1024) ? (largest_simd - 8 * 1024) : 0;
  ESP_LOGI(TAG, "Internal SRAM: %u KB free, SIMD block %u KB, giving %u KB to ESP-DL",
           (unsigned)(free_internal / 1024), (unsigned)(largest_simd / 1024),
           (unsigned)(max_internal / 1024));

  this->dl_model_ = new dl::Model(
      (const char *)model_data,
      fbs::MODEL_LOCATION_IN_FLASH_RODATA,
      max_internal,
      dl::MEMORY_MANAGER_GREEDY,
      nullptr,
      true
  );

  // Verify model loaded correctly (allocation failure leaves object in broken state)
  if (this->dl_model_ == nullptr || this->dl_model_->get_input() == nullptr) {
    ESP_LOGE(TAG, "Failed to create dl::Model (internal alloc may have failed)");
    // Retry with max_internal=0 (all PSRAM, slower but works)
    ESP_LOGW(TAG, "Retrying with PSRAM only (slower inference)...");
    delete this->dl_model_;
    this->dl_model_ = new dl::Model(
        (const char *)model_data,
        fbs::MODEL_LOCATION_IN_FLASH_RODATA,
        0,
        dl::MEMORY_MANAGER_GREEDY,
        nullptr,
        true
    );
    if (this->dl_model_ == nullptr || this->dl_model_->get_input() == nullptr) {
      ESP_LOGE(TAG, "Failed to create dl::Model even with PSRAM-only mode");
      this->mark_failed();
      return;
    }
    ESP_LOGI(TAG, "Model loaded in PSRAM-only mode (inference will be slower)");
  }
  }

  // Preprocessor: normalize [0,255] to [0,1] with std={255,255,255}
  // ESP32-P4 PPA outputs RGB565 in native LE byte order (RISC-V is little-endian)
  // DO NOT use DL_IMAGE_CAP_RGB565_BIG_ENDIAN - causes wrong channel extraction
  // (Center pixel [32,0,0] instead of proper RGB values → model sees garbage → 0 detections)
  uint32_t caps = 0;
#ifdef USE_YOLOV11_MIPI_CAMERA
  caps = 0;  // LE RGB565, no swap needed for standard RGB565 from PPA
  ESP_LOGI(TAG, "Preprocessor: P4 mode (RGB565 LE, caps=0)");
#else
  caps = 0;  // ESP32-S3 / standard esp32_camera: RGB565 LE
  ESP_LOGI(TAG, "Preprocessor: S3 mode (RGB565 LE, caps=0)");
#endif

  this->preprocessor_ = new dl::image::ImagePreprocessor(
      this->dl_model_, {0, 0, 0}, {255, 255, 255}, caps);
  this->preprocessor_->enable_letterbox({114, 114, 114});

  // Model-specific postprocessor
  if (this->model_type_ == MODEL_TYPE_YOLO26N) {
    this->yolo26n_postprocessor_ = new Yolo26nPostProcessor(
        this->dl_model_, this->preprocessor_,
        this->score_threshold_, 32);
    ESP_LOGI(TAG, "Using yolo26n postprocessor (anchor-free, top-32)");
  } else {
    this->postprocessor_ = new dl::detect::yolo11PostProcessor(
        this->dl_model_,
        this->preprocessor_,
        this->score_threshold_,
        this->nms_threshold_,
        0.7,
        {{8, 8, 4, 4}, {16, 16, 8, 8}, {32, 32, 16, 16}});
    ESP_LOGI(TAG, "Using yolo11 postprocessor (DFL + NMS)");
  }

  this->detector_initialized_ = true;
  auto *input_tensor = this->dl_model_->get_input();
  ESP_LOGI(TAG, "%s detector initialized (input: %dx%dx%d, exponent=%d)",
           model_type_str,
           (int)input_tensor->shape[2], (int)input_tensor->shape[1],
           (int)input_tensor->shape[3], input_tensor->exponent);
  ESP_LOGI(TAG, "Free heap: %lu, free PSRAM: %lu",
           (unsigned long)esp_get_free_heap_size(),
           (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif
}

void YOLOV11Component::loop() {
  if (!this->detector_initialized_) {
    return;
  }

#ifdef USE_YOLOV11_MIPI_CAMERA
  if (this->mipi_camera_ != nullptr) {
    if (!this->mipi_camera_->is_streaming()) {
      return;
    }

    // Wait until camera has produced at least one frame
    if (!this->first_frame_ready_) {
      auto *buf = this->mipi_camera_->acquire_buffer();
      if (buf == nullptr) {
        return;  // Camera not ready yet, silently wait
      }
      this->mipi_camera_->release_buffer(buf);
      this->first_frame_ready_ = true;
      ESP_LOGI(TAG, "Camera ready, starting detection");
    }

    // Auto-detect every N frames
    this->frame_counter_++;
    if (this->frame_counter_ < this->detection_interval_) {
      return;
    }
    this->frame_counter_ = 0;

    this->run_inference();
  }
#endif
  // For ESP32 camera, inference is handled via on_esp32_camera_image_ callback
}

void YOLOV11Component::run_inference() {
  if (!this->detector_initialized_) {
    return;
  }

#ifdef USE_YOLOV11_MIPI_CAMERA
  if (this->mipi_camera_ != nullptr) {
    if (!this->mipi_camera_->is_streaming()) {
      ESP_LOGW(TAG, "MIPI camera not streaming");
      return;
    }

    auto *buffer = this->mipi_camera_->acquire_buffer();
    if (buffer == nullptr) {
      ESP_LOGW(TAG, "Failed to acquire MIPI camera buffer");
      return;
    }

    uint8_t *img_data = this->mipi_camera_->get_buffer_data(buffer);
    uint16_t width = this->mipi_camera_->get_image_width();
    uint16_t height = this->mipi_camera_->get_image_height();

    if (img_data != nullptr) {
      // ESP32-P4: Invalidate CPU cache before reading SPIRAM buffer filled by DMA
      // Without this, CPU reads stale cached data → model sees blank/corrupted image
      uint32_t frame_size = width * height * 2;  // RGB565
      esp_cache_msync(img_data, frame_size,
                      ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);

      // Debug: analyze image brightness (every 10 inferences to track AE convergence)
      if (this->frame_counter_ == 0) {
        uint16_t *pixels = (uint16_t *)img_data;
        uint32_t total_pixels = width * height;
        uint32_t r_sum = 0, g_sum = 0, b_sum = 0;
        uint32_t sample_count = std::min(total_pixels, (uint32_t)10000);
        uint32_t step = total_pixels / sample_count;
        for (uint32_t i = 0; i < total_pixels; i += step) {
          uint16_t p = pixels[i];
          // LE RGB565 extraction (matches caps=0, official Espressif)
          r_sum += ((p >> 11) & 0x1F) << 3;
          g_sum += ((p >> 5) & 0x3F) << 2;
          b_sum += (p & 0x1F) << 3;
        }
        float r_avg = (float)r_sum / sample_count;
        float g_avg = (float)g_sum / sample_count;
        float b_avg = (float)b_sum / sample_count;

        // Also show raw bytes for first pixel to verify endianness
        uint8_t *raw = img_data;
        ESP_LOGI(TAG, "YOLO input: %ux%u, raw bytes[0-7]: %02X %02X %02X %02X %02X %02X %02X %02X",
                 width, height, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7]);
        ESP_LOGI(TAG, "  uint16 LE: %04X %04X %04X %04X", pixels[0], pixels[1], pixels[2], pixels[3]);
        ESP_LOGI(TAG, "  Avg RGB (LE decode): (%.0f, %.0f, %.0f) / 255", r_avg, g_avg, b_avg);
        // Check if image is mostly dark (AE not converged)
        float brightness = (r_avg + g_avg + b_avg) / 3.0f;
        if (brightness < 30) {
          ESP_LOGW(TAG, "  IMAGE VERY DARK (avg=%.0f) - auto-exposure may not have converged", brightness);
        }
      }

      this->detect_objects_(img_data, width, height);
    }

    this->mipi_camera_->release_buffer(buffer);
    return;
  }
#endif

#ifdef USE_YOLOV11_ESP32_CAMERA
  // ESP32 camera: inference is triggered via on_esp32_camera_image_ callback
  // request_inference() sets the flag, and the callback handles it
#endif
}

#ifdef USE_YOLOV11_ESP32_CAMERA
void YOLOV11Component::on_esp32_camera_image_(
    std::shared_ptr<esp32_camera::CameraImage> image) {
  if (!this->detector_initialized_ || !this->inference_requested_) {
    return;
  }

  this->inference_requested_ = false;

  uint8_t *data = image->get_data_buffer();
  size_t len = image->get_data_length();

  if (data == nullptr || len == 0) {
    return;
  }

  // Try to determine dimensions from data length (RGB565 = 2 bytes per pixel)
  const uint16_t resolutions[][2] = {
      {320, 240}, {640, 480}, {160, 120}, {800, 600}, {1024, 768},
  };

  bool found = false;
  for (auto &res : resolutions) {
    if (len == (size_t)res[0] * res[1] * 2) {
      this->detect_objects_(data, res[0], res[1]);
      found = true;
      break;
    }
  }
  if (!found) {
    ESP_LOGW(TAG, "Unsupported image size: %u bytes (need RGB565 format)", (unsigned)len);
  }
}
#endif

void YOLOV11Component::detect_objects_(uint8_t *rgb565_data, uint16_t width,
                                        uint16_t height) {
#ifdef ESP_DL_MODEL_YOLO11
  if (this->dl_model_ == nullptr) return;
  if (this->model_type_ == MODEL_TYPE_YOLO26N && this->yolo26n_postprocessor_ == nullptr) return;
  if (this->model_type_ == MODEL_TYPE_YOLO11 && this->postprocessor_ == nullptr) return;

  dl::image::img_t img;
  img.data = rgb565_data;
  img.width = width;
  img.height = height;
  img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565;

  uint32_t t0 = esp_log_timestamp();
  this->preprocessor_->preprocess(img);
  uint32_t t1 = esp_log_timestamp();

  // Debug: analyze model input tensor after preprocessing (first inference only)
  static bool input_tensor_logged = false;
  if (!input_tensor_logged) {
    input_tensor_logged = true;
    auto *input = this->dl_model_->get_input();
    int8_t *idata = (int8_t *)input->data;
    int H = input->shape[1], W = input->shape[2], C = input->shape[3];
    int total = H * W * C;
    // Histogram of quantized values
    int8_t imin = 127, imax = -128;
    int zero_count = 0, saturated_pos = 0, saturated_neg = 0;
    long sum = 0;
    for (int i = 0; i < total; i++) {
      int8_t v = idata[i];
      if (v < imin) imin = v;
      if (v > imax) imax = v;
      if (v == 0) zero_count++;
      if (v == 127) saturated_pos++;
      if (v == -128) saturated_neg++;
      sum += v;
    }
    float avg = (float)sum / total;
    ESP_LOGI(TAG, "Model INPUT tensor: shape=[%d,%d,%d,%d], exponent=%d",
             (int)input->shape[0], H, W, C, input->exponent);
    ESP_LOGI(TAG, "  Quantized stats: min=%d, max=%d, avg=%.1f", (int)imin, (int)imax, avg);
    ESP_LOGI(TAG, "  zeros=%d (%.1f%%), sat_127=%d (%.1f%%), sat_-128=%d (%.1f%%)",
             zero_count, 100.0f * zero_count / total,
             saturated_pos, 100.0f * saturated_pos / total,
             saturated_neg, 100.0f * saturated_neg / total);
    ESP_LOGI(TAG, "  First 16 values: %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
             idata[0], idata[1], idata[2], idata[3], idata[4], idata[5], idata[6], idata[7],
             idata[8], idata[9], idata[10], idata[11], idata[12], idata[13], idata[14], idata[15]);
    // Center pixel values (should be from actual image, not letterbox padding)
    int center = (H/2 * W + W/2) * C;
    ESP_LOGI(TAG, "  Center pixel RGB: %d %d %d", idata[center], idata[center+1], idata[center+2]);
  }

  this->dl_model_->run();
  uint32_t t2 = esp_log_timestamp();

  // Debug: check model output tensor values and score analysis
  {
    auto &outputs = this->dl_model_->get_outputs();
    ESP_LOGI(TAG, "Model outputs: %d tensors", (int)outputs.size());
    int idx = 0;
    for (auto &kv : outputs) {
      auto *tensor = kv.second;
      int total = 1;
      std::string shape_str;
      for (int d = 0; d < (int)tensor->shape.size(); d++) {
        total *= tensor->shape[d];
        if (d > 0) shape_str += "x";
        shape_str += std::to_string(tensor->shape[d]);
      }
      int8_t *data = (int8_t *)tensor->data;
      int8_t max_val = -128, min_val = 127;
      int check = std::min(total, 1000);
      for (int j = 0; j < check; j++) {
        if (data[j] > max_val) max_val = data[j];
        if (data[j] < min_val) min_val = data[j];
      }
      float scale = (tensor->exponent > 0) ? (float)(1 << tensor->exponent)
                                             : (1.0f / (float)(1 << -(tensor->exponent)));
      ESP_LOGI(TAG, "  Output[%d] '%s': shape=[%s], exponent=%d, scale=%.6f, range=[%d..%d]",
               idx++, kv.first.c_str(), shape_str.c_str(), tensor->exponent, scale, min_val, max_val);
    }

    // Detailed score analysis for score0 (largest feature map)
    auto *score0 = this->dl_model_->get_output("score0");
    if (score0 != nullptr) {
      float s_scale = (score0->exponent > 0) ? (float)(1 << score0->exponent)
                                               : (1.0f / (float)(1 << -(score0->exponent)));
      float inv_sigmoid_thr = -logf(1.0f / this->score_threshold_ - 1.0f);
      int8_t quant_thr = (int8_t)std::max(-128.0f, std::min(127.0f, roundf(inv_sigmoid_thr / s_scale)));

      int8_t *sdata = (int8_t *)score0->data;
      int H = score0->shape[1], W = score0->shape[2], C = score0->shape[3];
      int total_cells = H * W;
      int total_scores = H * W * C;

      // Find global max score and count above threshold
      int8_t global_max = -128;
      int above_thr = 0;
      int best_class = -1;
      int best_y = -1, best_x = -1;
      for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
          for (int c = 0; c < C; c++) {
            int8_t val = sdata[(y * W + x) * C + c];
            if (val > global_max) {
              global_max = val;
              best_class = c;
              best_y = y;
              best_x = x;
            }
            if (val > quant_thr) {
              above_thr++;
            }
          }
        }
      }
      float best_dequant = global_max * s_scale;
      float best_prob = 1.0f / (1.0f + expf(-best_dequant));
      ESP_LOGI(TAG, "Score0 analysis: %dx%dx%d=%d scores, exponent=%d, scale=%.6f",
               H, W, C, total_scores, score0->exponent, s_scale);
      ESP_LOGI(TAG, "  Threshold: score_thr=%.2f -> inverse_sigmoid=%.3f -> quant_thr=%d",
               this->score_threshold_, inv_sigmoid_thr, (int)quant_thr);
      ESP_LOGI(TAG, "  Best score: quant=%d, dequant=%.4f, sigmoid=%.4f, class=%d, pos=(%d,%d)",
               (int)global_max, best_dequant, best_prob, best_class, best_x, best_y);
      ESP_LOGI(TAG, "  Scores above threshold: %d / %d", above_thr, total_scores);
    }
  }

  uint32_t t3 = esp_log_timestamp();
  int raw_count = 0;

  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
    this->cached_detections_.clear();

    if (this->model_type_ == MODEL_TYPE_YOLO26N) {
      this->yolo26n_postprocessor_->clear_result();
      this->yolo26n_postprocessor_->postprocess();
      t3 = esp_log_timestamp();
      auto scaled = this->yolo26n_postprocessor_->get_results_scaled(width, height);
      raw_count = scaled.size();
      for (auto &det : scaled) {
        if (!this->is_class_allowed_(det.class_id)) continue;
        DetectionResult d;
        d.category = det.class_id;
        d.score = det.score;
        d.x1 = (int)det.x1;
        d.y1 = (int)det.y1;
        d.x2 = (int)det.x2;
        d.y2 = (int)det.y2;
        this->cached_detections_.push_back(d);
      }
    } else {
      this->postprocessor_->clear_result();
      this->postprocessor_->postprocess();
      t3 = esp_log_timestamp();
      auto &results = this->postprocessor_->get_result(width, height);
      raw_count = results.size();
      for (auto &result : results) {
        if (!this->is_class_allowed_(result.category)) continue;
        DetectionResult det;
        det.category = result.category;
        det.score = result.score;
        det.x1 = result.box[0];
        det.y1 = result.box[1];
        det.x2 = result.box[2];
        det.y2 = result.box[3];
        this->cached_detections_.push_back(det);
      }
    }

    xSemaphoreGive(this->detections_mutex_);
  }

  ESP_LOGI(TAG, "Timing: preprocess=%lums, inference=%lums, postprocess=%lums, total=%lums, detections=%d",
           (unsigned long)(t1 - t0), (unsigned long)(t2 - t1),
           (unsigned long)(t3 - t2), (unsigned long)(t3 - t0), raw_count);

  std::string class_str = this->get_detection_class_string();
  std::string bb_str = this->get_detection_bb_string();

  for (auto &callback : this->detection_class_callbacks_) {
    callback(class_str);
  }
  for (auto &callback : this->detection_bb_callbacks_) {
    callback(bb_str);
  }

  if (raw_count > 0) {
    ESP_LOGD(TAG, "Detected %d object(s): %s", raw_count,
             class_str.c_str());
  }
#endif
}

bool YOLOV11Component::is_class_allowed_(int category) const {
  // Empty set = all classes allowed
  if (this->detect_classes_.empty()) {
    return true;
  }
  return this->detect_classes_.count(category) > 0;
}

void YOLOV11Component::draw_on_frame(uint8_t *img_data, uint16_t width, uint16_t height) {
  if (!this->draw_enabled_) {
    return;
  }
  this->draw_results_(img_data, width, height);
}

void YOLOV11Component::draw_results_(uint8_t *img_data, uint16_t width, uint16_t height) {
  if (img_data == nullptr || this->detections_mutex_ == nullptr) {
    return;
  }

  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    const uint16_t COLOR_RED    = 0xF800;
    const uint16_t COLOR_GREEN  = 0x07E0;
    const uint16_t COLOR_BLUE   = 0x001F;
    const uint16_t COLOR_YELLOW = 0xFFE0;
    const uint16_t COLOR_CYAN   = 0x07FF;
    const uint16_t COLOR_MAGENTA = 0xF81F;
    const uint16_t COLOR_WHITE  = 0xFFFF;

    for (auto &det : this->cached_detections_) {
      int x1 = std::max(2, std::min(det.x1, (int)width - 3));
      int y1 = std::max(2, std::min(det.y1, (int)height - 3));
      int x2 = std::max(x1 + 10, std::min(det.x2, (int)width - 3));
      int y2 = std::max(y1 + 10, std::min(det.y2, (int)height - 3));

      // Color by category
      uint16_t color;
      switch (det.category) {
        case 0:  color = COLOR_RED; break;      // person
        case 1:  color = COLOR_GREEN; break;    // bicycle
        case 2:  color = COLOR_CYAN; break;     // car
        case 14: color = COLOR_MAGENTA; break;  // bird
        case 15: color = COLOR_BLUE; break;     // cat
        case 16: color = COLOR_GREEN; break;    // dog
        default: color = COLOR_YELLOW; break;
      }

      const int line_width = 2;
      uint16_t *buffer = (uint16_t *)img_data;

      // Draw bounding box - top and bottom lines
      for (int x = x1; x <= x2; x++) {
        for (int t = 0; t < line_width; t++) {
          int top = (y1 + t) * width + x;
          if (top >= 0 && top < width * height) buffer[top] = color;
          int bot = (y2 - t) * width + x;
          if (bot >= 0 && bot < width * height) buffer[bot] = color;
        }
      }
      // Left and right lines
      for (int y = y1; y <= y2; y++) {
        for (int t = 0; t < line_width; t++) {
          int left = y * width + (x1 + t);
          if (left >= 0 && left < width * height) buffer[left] = color;
          int right = y * width + (x2 - t);
          if (right >= 0 && right < width * height) buffer[right] = color;
        }
      }

      // Build label: "CLASS XX%"
      const char *class_name = this->get_class_name(det.category);
      char label[32];
      int pct = (int)(det.score * 100.0f);
      snprintf(label, sizeof(label), "%s %d%%", class_name, pct);

      // Label dimensions
      int label_len = strlen(label);
      int char_w = 6;  // 5px + 1px spacing
      int char_h = 9;  // 7px + 2px padding
      int label_w = label_len * char_w + 2;
      int label_h = char_h + 2;

      // Position: above the box, or inside if no room
      int label_x = x1;
      int label_y = y1 - label_h;
      if (label_y < 0) label_y = y1 + 2;  // Inside box if no room above

      // Draw label background (filled rectangle in box color)
      for (int by = std::max(0, label_y); by < std::min((int)height, label_y + label_h); by++) {
        for (int bx = std::max(0, label_x); bx < std::min((int)width, label_x + label_w); bx++) {
          buffer[by * width + bx] = color;
        }
      }

      // Draw text in white on colored background
      this->draw_text_(img_data, width, height, label_x + 1, label_y + 1, label, COLOR_WHITE, 1);
    }

    if (!this->cached_detections_.empty()) {
      ESP_LOGD(TAG, "Drew %d detection boxes with labels", (int)this->cached_detections_.size());
    }
    xSemaphoreGive(this->detections_mutex_);
  }
}

void YOLOV11Component::draw_char_(uint8_t *img_data, uint16_t img_width, uint16_t img_height,
                                   int x, int y, char c, uint16_t color, int scale) {
  int font_idx = -1;

  if (c >= 'A' && c <= 'Z') {
    font_idx = c - 'A';
  } else if (c >= 'a' && c <= 'z') {
    font_idx = c - 'a';  // Map to uppercase
  } else if (c >= '0' && c <= '9') {
    font_idx = 26 + (c - '0');
  } else if (c == ' ') {
    font_idx = 36;
  } else if (c == ':') {
    font_idx = 37;
  } else if (c == ',') {
    font_idx = 38;
  } else if (c == '%') {
    font_idx = 39;
  } else if (c == '.') {
    font_idx = 40;
  } else if (c == '-') {
    font_idx = 41;
  }

  if (font_idx < 0) return;

  int char_w = 5 * scale;
  int char_h = 7 * scale;
  if (x + char_w <= 0 || x >= img_width || y + char_h <= 0 || y >= img_height) return;

  uint16_t *buffer = (uint16_t *)img_data;

  for (int row = 0; row < 7; row++) {
    uint8_t row_data = FONT_5X7[font_idx][row];
    for (int col = 0; col < 5; col++) {
      if (row_data & (0x10 >> col)) {
        for (int sy = 0; sy < scale; sy++) {
          int py = y + row * scale + sy;
          if (py < 0 || py >= img_height) continue;
          for (int sx = 0; sx < scale; sx++) {
            int px = x + col * scale + sx;
            if (px >= 0 && px < img_width) {
              buffer[py * img_width + px] = color;
            }
          }
        }
      }
    }
  }
}

void YOLOV11Component::draw_text_(uint8_t *img_data, uint16_t img_width, uint16_t img_height,
                                   int x, int y, const char *text, uint16_t color, int scale) {
  int char_width = 6 * scale;  // 5 pixels + 1 spacing
  int current_x = x;

  for (const char *p = text; *p != '\0'; p++) {
    if (current_x + 5 * scale >= img_width) break;
    this->draw_char_(img_data, img_width, img_height, current_x, y, *p, color, scale);
    current_x += char_width;
  }
}

void YOLOV11Component::dump_config() {
  ESP_LOGCONFIG(TAG, "YOLOV11:");
  ESP_LOGCONFIG(TAG, "  Model type: %s",
                this->model_type_ == MODEL_TYPE_YOLO26N ? "yolo26n" : "yolo11");
#ifdef USE_YOLOV11_ESP32_CAMERA
  ESP_LOGCONFIG(TAG, "  Camera: ESP32 Camera (S3)");
#endif
#ifdef USE_YOLOV11_MIPI_CAMERA
  ESP_LOGCONFIG(TAG, "  Camera: MIPI DSI Camera (P4)");
#endif
  ESP_LOGCONFIG(TAG, "  Score threshold: %.2f", this->score_threshold_);
  ESP_LOGCONFIG(TAG, "  NMS threshold: %.2f", this->nms_threshold_);
  if (this->model_file_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Model size: %u bytes",
                  (unsigned)this->model_file_->get_size());
  }
  ESP_LOGCONFIG(TAG, "  Detection interval: %d", this->detection_interval_);
  ESP_LOGCONFIG(TAG, "  Draw enabled: %s", this->draw_enabled_ ? "YES" : "NO");
  ESP_LOGCONFIG(TAG, "  Classes: %d", (int)this->class_labels_.size());
  if (this->detect_classes_.empty()) {
    ESP_LOGCONFIG(TAG, "  Filter: ALL classes");
  } else {
    ESP_LOGCONFIG(TAG, "  Filter: %d class(es)", (int)this->detect_classes_.size());
    for (int id : this->detect_classes_) {
      ESP_LOGCONFIG(TAG, "    [%d] %s", id, this->get_class_name(id));
    }
  }
}

int YOLOV11Component::get_detected_count() {
  int count = 0;
  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    count = this->cached_detections_.size();
    xSemaphoreGive(this->detections_mutex_);
  }
  return count;
}

std::vector<DetectionResult> YOLOV11Component::get_detections() {
  std::vector<DetectionResult> detections;
  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    detections = this->cached_detections_;
    xSemaphoreGive(this->detections_mutex_);
  }
  return detections;
}

std::string YOLOV11Component::get_detection_class_string() {
  std::string result;
  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    for (size_t i = 0; i < this->cached_detections_.size(); i++) {
      auto &det = this->cached_detections_[i];
      if (i > 0)
        result += ",";
      char buf[64];
      snprintf(buf, sizeof(buf), "%s:%.0f%%",
               this->get_class_name(det.category), det.score * 100.0f);
      result += buf;
    }
    xSemaphoreGive(this->detections_mutex_);
  }
  return result;
}

std::string YOLOV11Component::get_detection_bb_string() {
  std::string result;
  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    for (size_t i = 0; i < this->cached_detections_.size(); i++) {
      auto &det = this->cached_detections_[i];
      if (i > 0)
        result += ",";
      char buf[64];
      snprintf(buf, sizeof(buf), "[%d,%d,%d,%d]",
               det.x1, det.y1, det.x2, det.y2);
      result += buf;
    }
    xSemaphoreGive(this->detections_mutex_);
  }
  return result;
}

}  // namespace yolov11
}  // namespace esphome
