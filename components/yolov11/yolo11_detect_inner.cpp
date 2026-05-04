// Self-contained copy of yolo11_detect.cpp. See the matching .hpp for
// rationale. We keep the SD-card path branch even though yolov11
// targets flash-rodata only; with CONFIG_YOLO11_DETECT_MODEL_IN_FLASH_RODATA=1
// the SD branch compiles out, so it's free.
#include "yolo11_detect.hpp"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#if CONFIG_YOLO11_DETECT_MODEL_IN_FLASH_RODATA
extern const uint8_t yolo11_detect_espdl[] asm("_binary_yolo11_detect_espdl_start");
static const char *path = (const char *)yolo11_detect_espdl;
#elif CONFIG_YOLO11_DETECT_MODEL_IN_FLASH_PARTITION
static const char *path = "yolo11_detect";
#endif

namespace yolo11_detect {

YOLO11Impl::YOLO11Impl(const char *model_name)
{
#if !CONFIG_YOLO11_DETECT_MODEL_IN_SDCARD
    m_model = new dl::Model(
        path, model_name, static_cast<fbs::model_location_type_t>(CONFIG_YOLO11_DETECT_MODEL_LOCATION));
#else
    m_model =
        new dl::Model(model_name, static_cast<fbs::model_location_type_t>(CONFIG_YOLO11_DETECT_MODEL_LOCATION));
#endif

#if CONFIG_IDF_TARGET_ESP32P4
    m_image_preprocessor =
        new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {255, 255, 255});
#else
    // ESP32-S3 path - DVP camera delivers RGB565 big-endian
    m_image_preprocessor = new dl::image::ImagePreprocessor(
        m_model, {0, 0, 0}, {255, 255, 255}, DL_IMAGE_CAP_RGB565_BIG_ENDIAN);
#endif

    m_postprocessor =
        new dl::detect::yolo11PostProcessor(m_model, m_image_preprocessor, 0.3, 0.3, 10,
            {{8, 8, 4, 4}, {16, 16, 8, 8}, {32, 32, 16, 16}});
}

} // namespace yolo11_detect

YOLO11Detect::YOLO11Detect(const char *sdcard_model_dir, model_type_t model_type)
{
    (void)sdcard_model_dir;
    switch (model_type) {
    case model_type_t::YOLO11_S8_V1:
#if CONFIG_YOLO11_DETECT_S8_V1
        m_model = new yolo11_detect::YOLO11Impl("yolo11_detect_s8_v1.espdl");
#else
        ESP_LOGE("yolo11_detect", "yolo11_detect_s8_v1 is not selected in menuconfig.");
#endif
        break;
    }
}
