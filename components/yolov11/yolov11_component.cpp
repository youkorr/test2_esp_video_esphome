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

  ESP_LOGI(TAG, "Loading model (%u bytes)...", (unsigned)model_size);

  // Internal RAM too limited (~31KB free after system/LVGL/WiFi), use SPIRAM for all tensors
  this->dl_model_ = new dl::Model(
      (const char *)model_data,
      fbs::MODEL_LOCATION_IN_FLASH_RODATA,
      0,
      dl::MEMORY_MANAGER_GREEDY,
      nullptr,
      true
  );

  if (this->dl_model_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create dl::Model");
    this->mark_failed();
    return;
  }

  // Official esp-dl YOLO11 preprocessing: mean={0,0,0}, std={1,1,1}
  // The .espdl model handles quantization internally via tensor exponents.
  // ESP32-P4 MIPI CSI camera stores RGB565 big-endian in memory
  this->preprocessor_ = new dl::image::ImagePreprocessor(
      this->dl_model_, {0, 0, 0}, {1, 1, 1},
      dl::image::DL_IMAGE_CAP_RGB_SWAP | dl::image::DL_IMAGE_CAP_RGB565_BIG_ENDIAN);
  // Standard YOLO letterbox padding (gray 114,114,114) for non-square input images
  this->preprocessor_->enable_letterbox({114, 114, 114});

  this->postprocessor_ = new dl::detect::yolo11PostProcessor(
      this->dl_model_,
      this->preprocessor_,
      this->score_threshold_,
      this->nms_threshold_,
      10,
      {{8, 8, 4, 4}, {16, 16, 8, 8}, {32, 32, 16, 16}});

  this->detector_initialized_ = true;
  auto *input_tensor = this->dl_model_->get_input();
  ESP_LOGI(TAG, "YOLO11 detector initialized (model input: %dx%d)",
           (int)input_tensor->shape[2], (int)input_tensor->shape[1]);
  ESP_LOGI(TAG, "  Input tensor: dtype=%d, exponent=%d, shape=[%d,%d,%d,%d]",
           (int)input_tensor->dtype, input_tensor->exponent,
           (int)input_tensor->shape[0], (int)input_tensor->shape[1],
           (int)input_tensor->shape[2], (int)input_tensor->shape[3]);
  // CRITICAL: The exponent determines how pixel values are quantized to int8.
  // With mean=0, std=1: quant_value = clamp(pixel * (1 << -exponent), -128, 127)
  // If exponent=-7: pixel=1 → 1*128=128 → clamped to 127 (ALL pixels > 0 become 127!)
  // If exponent=0:  pixel=200 → 200*1=200 → clamped to 127 (values > 127 clip)
  // The correct std depends on the exponent:
  //   exponent=-7 → model expects [0,1] range → need std={255,255,255}
  //   exponent=0  → model expects [0,255] range → need std={1,1,1}
  if (input_tensor->exponent < -1) {
    ESP_LOGW(TAG, "  WARNING: Input exponent=%d means model expects normalized [0,1] input!",
             input_tensor->exponent);
    ESP_LOGW(TAG, "  With std={1,1,1}, ALL pixels >0 saturate to 127 (binary image)!");
    ESP_LOGW(TAG, "  This is likely the cause of zero detections.");
  }
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
  if (this->dl_model_ == nullptr || this->postprocessor_ == nullptr) {
    return;
  }

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

  this->postprocessor_->clear_result();
  this->postprocessor_->postprocess();
  uint32_t t3 = esp_log_timestamp();
  auto &results = this->postprocessor_->get_result(width, height);
  ESP_LOGI(TAG, "Timing: preprocess=%lums, inference=%lums, postprocess=%lums, total=%lums, raw_detections=%d",
           (unsigned long)(t1 - t0), (unsigned long)(t2 - t1),
           (unsigned long)(t3 - t2), (unsigned long)(t3 - t0), (int)results.size());

  for (auto &result : results) {
    ESP_LOGI(TAG, "  Raw det: cat=%d score=%.2f box=[%d,%d,%d,%d]",
             result.category, result.score,
             result.box[0], result.box[1], result.box[2], result.box[3]);
  }

  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
    this->cached_detections_.clear();

    for (auto &result : results) {
      // Runtime class filtering
      if (!this->is_class_allowed_(result.category)) {
        continue;
      }
      DetectionResult det;
      det.category = result.category;
      det.score = result.score;
      det.x1 = result.box[0];
      det.y1 = result.box[1];
      det.x2 = result.box[2];
      det.y2 = result.box[3];
      this->cached_detections_.push_back(det);
    }

    xSemaphoreGive(this->detections_mutex_);
  }

  std::string class_str = this->get_detection_class_string();
  std::string bb_str = this->get_detection_bb_string();

  for (auto &callback : this->detection_class_callbacks_) {
    callback(class_str);
  }
  for (auto &callback : this->detection_bb_callbacks_) {
    callback(bb_str);
  }

  if (!results.empty()) {
    ESP_LOGD(TAG, "Detected %d object(s): %s", (int)results.size(),
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

      // Top and bottom lines
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
    }

    xSemaphoreGive(this->detections_mutex_);
  }
}

void YOLOV11Component::dump_config() {
  ESP_LOGCONFIG(TAG, "YOLOV11:");
#ifdef USE_YOLOV11_ESP32_CAMERA
  ESP_LOGCONFIG(TAG, "  Camera: ESP32 Camera");
#endif
#ifdef USE_YOLOV11_MIPI_CAMERA
  ESP_LOGCONFIG(TAG, "  Camera: MIPI DSI Camera (esp_cam_sensor)");
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
