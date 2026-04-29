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
    // Camera produces RGB565 little-endian (CSI_BYTE_SWAP_EN = false)
    // So we don't use DL_IMAGE_CAP_RGB565_BIG_ENDIAN flag
    m_image_preprocessor =
        new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {1, 1, 1});
#else
    m_image_preprocessor = new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {1, 1, 1});
#endif

    // YOLO11 postprocessor configuration
    // Anchors for 3 detection stages (8x8, 16x16, 32x32 strides)
    m_postprocessor =
        new dl::detect::yolo11PostProcessor(m_model, m_image_preprocessor, 0.3, 0.3, 10,
            {{8, 8, 4, 4}, {16, 16, 8, 8}, {32, 32, 16, 16}});
}

#if CONFIG_YOLO11_DETECT_MODEL_IN_SDCARD
// Build the absolute path of the model file given what the user passed
// in `model_path`. Two forms are supported:
//   1. A directory (e.g. "/sdcard/models/p4")    -> we append the default
//      filename for the variant.
//   2. A full file path (e.g. ".../foo.espdl")   -> we use it as-is.
// We also try a few common alternative file names for variant V1, so the
// user can drop in either the official esp32_p4_eye model
// (`coco_detect_yolo11n_s8_v1.espdl`) or the test2 model
// (`yolo11_detect_s8_v1.espdl`) without renaming.
static bool resolve_sdcard_model_path(const char *user_path,
                                      const char *const *fallback_filenames,
                                      char *out, size_t out_size)
{
    if (user_path == nullptr || user_path[0] == '\0') {
        return false;
    }
    size_t len = strlen(user_path);
    bool looks_like_file = (len > 6 && strcmp(user_path + len - 6, ".espdl") == 0);

    struct stat st;
    if (looks_like_file) {
        if (stat(user_path, &st) == 0) {
            snprintf(out, out_size, "%s", user_path);
            return true;
        }
        ESP_LOGW("yolo11_detect", "model file does not exist: %s", user_path);
        return false;
    }

    // Treat as a directory: try each fallback name in order.
    for (size_t i = 0; fallback_filenames[i] != nullptr; i++) {
        snprintf(out, out_size, "%s/%s", user_path, fallback_filenames[i]);
        if (stat(out, &st) == 0) {
            ESP_LOGI("yolo11_detect", "found model: %s", out);
            return true;
        }
    }

    ESP_LOGE("yolo11_detect", "no model file found in directory %s", user_path);
    ESP_LOGE("yolo11_detect", "tried (in order):");
    for (size_t i = 0; fallback_filenames[i] != nullptr; i++) {
        ESP_LOGE("yolo11_detect", "  %s/%s", user_path, fallback_filenames[i]);
    }
    return false;
}
#endif

} // namespace yolo11_detect

YOLO11Detect::YOLO11Detect(const char *sdcard_model_dir, model_type_t model_type)
{
    switch (model_type) {
    case model_type_t::YOLO11_S8_V1:
#if CONFIG_YOLO11_DETECT_S8_V1
#if !CONFIG_YOLO11_DETECT_MODEL_IN_SDCARD
        m_model = new yolo11_detect::YOLO11Impl("yolo11_detect_s8_v1.espdl");
#else
        if (sdcard_model_dir) {
            // Accept either the test2-style filename or the upstream
            // esp32_p4_eye filenames so users can drop in whichever model
            // file they already have on the SD card.
            static const char *const v1_filenames[] = {
                "yolo11_detect_s8_v1.espdl",
                "coco_detect_yolo11n_s8_v1.espdl",
                "coco_detect_yolo11n_s8_v2.espdl",
                "coco_detect_yolo11n_s8_v3.espdl",
                "coco_detect_yolo11n_320_s8_v3.espdl",
                nullptr,
            };
            char resolved[256];
            if (yolo11_detect::resolve_sdcard_model_path(sdcard_model_dir,
                                                         v1_filenames,
                                                         resolved, sizeof(resolved))) {
                m_model = new yolo11_detect::YOLO11Impl(resolved);
            } else {
                ESP_LOGE("yolo11_detect", "could not locate a YOLO11 model on the SD card");
            }
        } else {
            ESP_LOGE("yolo11_detect", "please pass sdcard mount point as parameter.");
        }
#endif
#else
        ESP_LOGE("yolo11_detect", "yolo11_detect_s8_v1 is not selected in menuconfig.");
#endif
        break;
    }
}
