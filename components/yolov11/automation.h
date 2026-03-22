#pragma once

#include "yolov11_component.h"
#include "esphome/core/automation.h"

namespace esphome {
namespace yolov11 {

template<typename... Ts>
class YOLOV11InferenceAction : public Action<Ts...> {
 public:
  // Template constructor for codegen compatibility
  template<typename T>
  explicit YOLOV11InferenceAction(T) {}

  void set_parent(YOLOV11Component *parent) { this->parent_ = parent; }

  void play(Ts... x) override {
    if (this->parent_ != nullptr) {
      this->parent_->request_inference();
    }
  }

 protected:
  YOLOV11Component *parent_{nullptr};
};

}  // namespace yolov11
}  // namespace esphome
