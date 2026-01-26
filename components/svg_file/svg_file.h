#pragma once

#include "esphome/core/component.h"

namespace esphome {
namespace svg_file {

/**
 * SvgFile - Embedded SVG vector graphic in Flash ROM
 *
 * Stores SVG XML data in program memory (Flash).
 * Can be referenced by LVGL img widget for rendering.
 */
class SvgFile {
 public:
  SvgFile(const uint8_t *data_start, size_t data_len) : data_start_(data_start), data_len_(data_len) {}

  /**
   * Get pointer to the SVG XML data in Flash ROM
   * @return Pointer to the start of the XML string
   */
  const uint8_t *get_data() const { return this->data_start_; }

  /**
   * Get the size of the SVG data in bytes
   * @return Size in bytes
   */
  size_t get_size() const { return this->data_len_; }

 protected:
  const uint8_t *data_start_;
  size_t data_len_;
};

}  // namespace svg_file
}  // namespace esphome
