// Self-contained copy of yolo11_detect.cpp with one extension: a
// runtime model-data override. By default the model bytes come from
// the build-embedded `_binary_yolo11_detect_espdl_start` symbol, but
// before the YOLO11Detect / YOLO11Impl constructors are called the
// outer YOLOv11Component can swap that pointer for a buffer loaded at
// runtime (e.g. from jesserockz's `file:` platform). This is what
// lets users fine-tune YOLO on their own dataset and drop the new
// .espdl on the device without reflashing the firmware.

#include "yolo11_detect.hpp"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#if CONFIG_YOLO11_DETECT_MODEL_IN_FLASH_RODATA
extern const uint8_t yolo11_detect_espdl[] asm("_binary_yolo11_detect_espdl_start");
static const uint8_t *flash_rodata_default = yolo11_detect_espdl;
#elif CONFIG_YOLO11_DETECT_MODEL_IN_FLASH_PARTITION
static const uint8_t *flash_rodata_default = nullptr;
static const char *partition_name = "yolo11_detect";
#endif

// Runtime override (nullable). When non-null, YOLO11Impl will load the
// model from this buffer instead of the build-embedded one. Set via
// yolov11_set_runtime_model_data() before constructing YOLO11Detect.
static const uint8_t *runtime_model_data = nullptr;

extern "C" void yolov11_set_runtime_model_data(const uint8_t *data) {
    runtime_model_data = data;
    if (data) {
        ESP_LOGI("yolo11_detect", "runtime model override set: %p", data);
    } else {
        ESP_LOGI("yolo11_detect", "runtime model override cleared (using flash rodata)");
    }
}

namespace yolo11_detect {

YOLO11Impl::YOLO11Impl(const char *model_name)
{
#if !CONFIG_YOLO11_DETECT_MODEL_IN_SDCARD
    // Pick the runtime override if present, otherwise the build-embedded
    // flash rodata buffer.
    const uint8_t *bytes = runtime_model_data ? runtime_model_data
                                              : flash_rodata_default;
    if (bytes == nullptr) {
        ESP_LOGE("yolo11_detect", "no model data available (neither flash "
                                  "rodata nor runtime override)");
    }
    m_model = new dl::Model(
        reinterpret_cast<const char *>(bytes), model_name,
        static_cast<fbs::model_location_type_t>(CONFIG_YOLO11_DETECT_MODEL_LOCATION));
#else
    m_model = new dl::Model(
        model_name,
        static_cast<fbs::model_location_type_t>(CONFIG_YOLO11_DETECT_MODEL_LOCATION));
#endif

#if CONFIG_IDF_TARGET_ESP32P4
    m_image_preprocessor =
        new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {255, 255, 255});
#else
    // ESP32-S3 path - DVP camera delivers RGB565 big-endian
    m_image_preprocessor = new dl::image::ImagePreprocessor(
        m_model, {0, 0, 0}, {255, 255, 255}, dl::image::DL_IMAGE_CAP_RGB565_BIG_ENDIAN);
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
