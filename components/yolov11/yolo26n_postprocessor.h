#pragma once

#ifdef ESP_DL_MODEL_YOLO11

#include "dl_model_base.hpp"
#include "dl_image_preprocessor.hpp"
#include "dl_tensor_base.hpp"
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
#include "esp_log.h"

namespace esphome {
namespace yolov11 {

struct Yolo26nDetection {
  float x1, y1, x2, y2;
  float score;
  int class_id;
};

class Yolo26nPostProcessor {
 public:
  Yolo26nPostProcessor(dl::Model *model, dl::image::ImagePreprocessor *preprocessor,
                       float score_threshold, int max_detections)
      : model_(model), preprocessor_(preprocessor),
        score_threshold_(score_threshold), max_detections_(max_detections) {}

  void clear_result() { results_.clear(); }

  void postprocess() {
    results_.clear();
    auto &outputs = model_->get_outputs();

    // yolo26n outputs: one2one_p3_box, one2one_p4_box, one2one_p5_box,
    //                  one2one_p3_cls, one2one_p4_cls, one2one_p5_cls
    const char *box_names[] = {"one2one_p3_box", "one2one_p4_box", "one2one_p5_box"};
    const char *cls_names[] = {"one2one_p3_cls", "one2one_p4_cls", "one2one_p5_cls"};
    const int strides[] = {8, 16, 32};

    for (int i = 0; i < 3; i++) {
      auto box_it = outputs.find(box_names[i]);
      auto cls_it = outputs.find(cls_names[i]);
      if (box_it == outputs.end() || cls_it == outputs.end()) {
        ESP_LOGW("yolo26n", "Missing output tensor: %s or %s", box_names[i], cls_names[i]);
        continue;
      }

      dl::TensorBase *box_tensor = box_it->second;
      dl::TensorBase *cls_tensor = cls_it->second;

      int grid_h = cls_tensor->shape[1];
      int grid_w = cls_tensor->shape[2];
      int num_classes = cls_tensor->shape[3];

      float box_scale = std::pow(2.0f, box_tensor->exponent);
      float cls_scale = std::pow(2.0f, cls_tensor->exponent);

      // Quantized threshold for fast integer-domain filtering
      float raw_thresh = -std::log(1.0f / score_threshold_ - 1.0f);
      int8_t cls_thresh_int8 = (int8_t)std::max(-128.0f, std::min(127.0f, std::floor(raw_thresh / cls_scale)));

      int8_t *box_data = (int8_t *)box_tensor->data;
      int8_t *cls_data = (int8_t *)cls_tensor->data;

      for (int h = 0; h < grid_h; h++) {
        for (int w = 0; w < grid_w; w++) {
          int pixel_idx = h * grid_w + w;
          int cls_offset = pixel_idx * num_classes;

          // Find best class with fast int8 threshold check
          float max_score = -1.0f;
          int best_cls = -1;
          for (int c = 0; c < num_classes; c++) {
            int8_t raw_val = cls_data[cls_offset + c];
            if (raw_val <= cls_thresh_int8) continue;

            float dequant = raw_val * cls_scale;
            float score = 1.0f / (1.0f + std::exp(-dequant));
            if (score > max_score) {
              max_score = score;
              best_cls = c;
            }
          }

          if (max_score < score_threshold_) continue;

          // Decode box: d_l, d_t, d_r, d_b distances from grid center
          int box_offset = pixel_idx * 4;
          float d_l = box_data[box_offset + 0] * box_scale;
          float d_t = box_data[box_offset + 1] * box_scale;
          float d_r = box_data[box_offset + 2] * box_scale;
          float d_b = box_data[box_offset + 3] * box_scale;

          float cx = w + 0.5f;
          float cy = h + 0.5f;
          float x1 = (cx - d_l) * strides[i];
          float y1 = (cy - d_t) * strides[i];
          float x2 = (cx + d_r) * strides[i];
          float y2 = (cy + d_b) * strides[i];

          results_.push_back({x1, y1, x2, y2, max_score, best_cls});
        }
      }
    }

    // Sort by score descending, keep top-K
    std::sort(results_.begin(), results_.end(),
              [](const Yolo26nDetection &a, const Yolo26nDetection &b) {
                return a.score > b.score;
              });
    if ((int)results_.size() > max_detections_) {
      results_.resize(max_detections_);
    }
  }

  const std::vector<Yolo26nDetection> &get_results() const { return results_; }

  // Get results scaled to original image dimensions (accounting for letterbox)
  std::vector<Yolo26nDetection> get_results_scaled(uint16_t orig_w, uint16_t orig_h) const {
    auto *input = model_->get_input();
    int model_w = input->shape[2];
    int model_h = input->shape[1];

    // Get letterbox scaling info from preprocessor
    float scale_x = preprocessor_->get_resize_scale_x(true);  // inverse
    float scale_y = preprocessor_->get_resize_scale_y(true);  // inverse
    int border_top = preprocessor_->get_border_top();
    int border_left = preprocessor_->get_border_left();

    std::vector<Yolo26nDetection> scaled;
    scaled.reserve(results_.size());

    for (auto &det : results_) {
      Yolo26nDetection s = det;
      s.x1 = (det.x1 - border_left) * scale_x;
      s.y1 = (det.y1 - border_top) * scale_y;
      s.x2 = (det.x2 - border_left) * scale_x;
      s.y2 = (det.y2 - border_top) * scale_y;

      // Clamp to image bounds
      s.x1 = std::max(0.0f, std::min(s.x1, (float)orig_w));
      s.y1 = std::max(0.0f, std::min(s.y1, (float)orig_h));
      s.x2 = std::max(0.0f, std::min(s.x2, (float)orig_w));
      s.y2 = std::max(0.0f, std::min(s.y2, (float)orig_h));

      scaled.push_back(s);
    }
    return scaled;
  }

 private:
  dl::Model *model_;
  dl::image::ImagePreprocessor *preprocessor_;
  float score_threshold_;
  int max_detections_;
  std::vector<Yolo26nDetection> results_;
};

}  // namespace yolov11
}  // namespace esphome

#endif  // ESP_DL_MODEL_YOLO11
