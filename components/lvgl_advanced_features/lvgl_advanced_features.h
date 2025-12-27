#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace lvgl_advanced_features {

class LVGLAdvancedFeatures : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  // ThorVG configuration
  void set_thorvg_internal(bool enabled) { this->thorvg_internal_ = enabled; }
  void set_thorvg_external(bool enabled) { this->thorvg_external_ = enabled; }

  // Feature flags
  void set_svg_enabled(bool enabled) { this->svg_enabled_ = enabled; }
  void set_lottie_enabled(bool enabled) { this->lottie_enabled_ = enabled; }
  void set_freetype_enabled(bool enabled) { this->freetype_enabled_ = enabled; }
  void set_rlottie_enabled(bool enabled) { this->rlottie_enabled_ = enabled; }
  void set_ffmpeg_enabled(bool enabled) { this->ffmpeg_enabled_ = enabled; }

  // Image format support
  void set_libpng_enabled(bool enabled) { this->libpng_enabled_ = enabled; }
  void set_libjpeg_turbo_enabled(bool enabled) { this->libjpeg_turbo_enabled_ = enabled; }
  void set_gif_enabled(bool enabled) { this->gif_enabled_ = enabled; }
  void set_bmp_enabled(bool enabled) { this->bmp_enabled_ = enabled; }

  // Widgets
  void set_qrcode_enabled(bool enabled) { this->qrcode_enabled_ = enabled; }
  void set_barcode_enabled(bool enabled) { this->barcode_enabled_ = enabled; }
  void set_ime_pinyin_enabled(bool enabled) { this->ime_pinyin_enabled_ = enabled; }

 protected:
  bool thorvg_internal_{false};
  bool thorvg_external_{false};
  bool svg_enabled_{false};
  bool lottie_enabled_{false};
  bool freetype_enabled_{false};
  bool rlottie_enabled_{false};
  bool ffmpeg_enabled_{false};
  bool libpng_enabled_{false};
  bool libjpeg_turbo_enabled_{false};
  bool gif_enabled_{false};
  bool bmp_enabled_{false};
  bool qrcode_enabled_{false};
  bool barcode_enabled_{false};
  bool ime_pinyin_enabled_{false};
};

}  // namespace lvgl_advanced_features
}  // namespace esphome
