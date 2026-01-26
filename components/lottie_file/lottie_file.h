#pragma once

#include "esphome/core/component.h"

namespace esphome {
namespace lottie_file {

/**
 * LottieFile - Embedded Lottie animation in Flash ROM
 *
 * Stores Lottie JSON animation data in program memory (Flash).
 * Can be referenced by LVGL lottie widget for playback.
 */
class LottieFile {
 public:
  LottieFile(const uint8_t *data_start, size_t data_len) : data_start_(data_start), data_len_(data_len) {}

  /**
   * Get pointer to the JSON data in Flash ROM
   * @return Pointer to the start of the JSON string
   */
  const uint8_t *get_data() const { return this->data_start_; }

  /**
   * Get the size of the JSON data in bytes
   * @return Size in bytes
   */
  size_t get_size() const { return this->data_len_; }

 protected:
  const uint8_t *data_start_;
  size_t data_len_;
};

}  // namespace lottie_file
}  // namespace esphome
