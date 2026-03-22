#pragma once

#include "esphome/core/component.h"

namespace esphome {
namespace file_component {

class FileData : public Component {
 public:
  void set_data(const uint8_t *data, size_t size) {
    this->data_ = data;
    this->size_ = size;
  }

  const uint8_t *get_data() const { return this->data_; }
  size_t get_size() const { return this->size_; }

  void setup() override {}
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  const uint8_t *data_{nullptr};
  size_t size_{0};
};

}  // namespace file_component
}  // namespace esphome
