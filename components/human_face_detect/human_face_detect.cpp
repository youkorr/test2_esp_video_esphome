#include "human_face_detect.hpp"
#include "esp_log.h"

static const char *TAG_HFD = "human_face_detect";

#if CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA
extern const uint8_t human_face_detect_espdl[] asm("_binary_human_face_detect_espdl_start");
static const char *path = (const char *)human_face_detect_espdl;
#elif CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_PARTITION
static const char *path = "human_face_det";
#endif
namespace human_face_detect {

MSR::MSR(const char *model_name)
{
#if !CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD
    m_model = new dl::Model(
        path, model_name, static_cast<fbs::model_location_type_t>(CONFIG_HUMAN_FACE_DETECT_MODEL_LOCATION));
#else
    m_model =
        new dl::Model(model_name, static_cast<fbs::model_location_type_t>(CONFIG_HUMAN_FACE_DETECT_MODEL_LOCATION));
#endif

    // Log model input shape
    {
        auto *input = m_model->get_input();
        if (input && input->shape.size() >= 4) {
            ESP_LOGI(TAG_HFD, "MSR model input: [%d, %d, %d, %d] dtype=%d exponent=%d",
                     (int)input->shape[0], (int)input->shape[1],
                     (int)input->shape[2], (int)input->shape[3],
                     (int)input->dtype, (int)input->exponent);
        } else {
            ESP_LOGW(TAG_HFD, "MSR model input shape unavailable!");
        }
    }

#if CONFIG_IDF_TARGET_ESP32P4
    // ESP32-P4 MIPI CSI camera stores RGB565 big-endian in memory
    ESP_LOGI(TAG_HFD, "MSR: Using RGB565 BIG ENDIAN + RGB_SWAP (ESP32-P4 caps=0x%x)",
             (unsigned)(dl::image::DL_IMAGE_CAP_RGB_SWAP | dl::image::DL_IMAGE_CAP_RGB565_BIG_ENDIAN));
    m_image_preprocessor = new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {1, 1, 1},
        dl::image::DL_IMAGE_CAP_RGB_SWAP | dl::image::DL_IMAGE_CAP_RGB565_BIG_ENDIAN);
#else
    ESP_LOGI(TAG_HFD, "MSR: Using RGB_SWAP only (default mode)");
    m_image_preprocessor = new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {1, 1, 1}, dl::image::DL_IMAGE_CAP_RGB_SWAP);
#endif

    m_postprocessor = new dl::detect::MSRPostprocessor(
        m_model, m_image_preprocessor, 0.1, 0.5, 10, {{8, 8, 9, 9, {{16, 16}, {32, 32}}}, {16, 16, 9, 9, {{64, 64}, {128, 128}}}});
}

MNP::MNP(const char *model_name)
{
#if !CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD
    m_model = new dl::Model(
        path, model_name, static_cast<fbs::model_location_type_t>(CONFIG_HUMAN_FACE_DETECT_MODEL_LOCATION));
#else
    m_model =
        new dl::Model(model_name, static_cast<fbs::model_location_type_t>(CONFIG_HUMAN_FACE_DETECT_MODEL_LOCATION));
#endif

#if CONFIG_IDF_TARGET_ESP32P4
    ESP_LOGI(TAG_HFD, "MNP: Using RGB565 BIG ENDIAN + RGB_SWAP (ESP32-P4 mode)");
    m_image_preprocessor = new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {1, 1, 1},
        dl::image::DL_IMAGE_CAP_RGB_SWAP | dl::image::DL_IMAGE_CAP_RGB565_BIG_ENDIAN);
#else
    ESP_LOGI(TAG_HFD, "MNP: Using RGB_SWAP only (default mode)");
    m_image_preprocessor = new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {1, 1, 1}, dl::image::DL_IMAGE_CAP_RGB_SWAP);
#endif
    m_postprocessor = new dl::detect::MNPPostprocessor(m_model, m_image_preprocessor, 0.2, 0.5, 10, {{1, 1, 0, 0, {{48, 48}}}});
}

MNP::~MNP()
{
    if (m_model) {
        delete m_model;
        m_model = nullptr;
    }
    if (m_image_preprocessor) {
        delete m_image_preprocessor;
        m_image_preprocessor = nullptr;
    }
    if (m_postprocessor) {
        delete m_postprocessor;
        m_postprocessor = nullptr;
    }
};

std::list<dl::detect::result_t> &MNP::run(const dl::image::img_t &img, std::list<dl::detect::result_t> &candidates)
{
    dl::tool::Latency latency[3] = {dl::tool::Latency(10), dl::tool::Latency(10), dl::tool::Latency(10)};
    m_postprocessor->clear_result();
    for (auto &candidate : candidates) {
        int center_x = (candidate.box[0] + candidate.box[2]) >> 1;
        int center_y = (candidate.box[1] + candidate.box[3]) >> 1;
        int side = DL_MAX(candidate.box[2] - candidate.box[0], candidate.box[3] - candidate.box[1]);
        candidate.box[0] = center_x - (side >> 1);
        candidate.box[1] = center_y - (side >> 1);
        candidate.box[2] = candidate.box[0] + side;
        candidate.box[3] = candidate.box[1] + side;
        candidate.limit_box(img.width, img.height);

        latency[0].start();
        m_image_preprocessor->preprocess(img, candidate.box);
        latency[0].end();

        latency[1].start();
        m_model->run();
        latency[1].end();

        latency[2].start();
        m_postprocessor->postprocess();
        latency[2].end();
    }
    m_postprocessor->nms();
    std::list<dl::detect::result_t> &result = m_postprocessor->get_result(img.width, img.height);
    if (candidates.size() > 0) {
        latency[0].print("detect", "preprocess");
        latency[1].print("detect", "forward");
        latency[2].print("detect", "postprocess");
    }

    return result;
}

MSRMNP::~MSRMNP()
{
    if (m_msr) {
        delete m_msr;
        m_msr = nullptr;
    }
    if (m_mnp) {
        delete m_mnp;
        m_mnp = nullptr;
    }
}

std::list<dl::detect::result_t> &MSRMNP::run(const dl::image::img_t &img)
{
    static int msrmnp_log_count = 0;
    std::list<dl::detect::result_t> &candidates = m_msr->run(img);
    if (msrmnp_log_count < 5) {
        msrmnp_log_count++;
        ESP_LOGI(TAG_HFD, "MSR stage: %d candidates from %dx%d image (run %d)",
                 (int)candidates.size(), img.width, img.height, msrmnp_log_count);

        // Inspect raw model output scores to diagnose detection
        dl::Model *model = m_msr->get_raw_model(0);
        if (model) {
            for (const char *name : {"score0", "score1"}) {
                auto *tensor = model->get_output(name);
                if (tensor && tensor->data) {
                    int total = 1;
                    for (auto s : tensor->shape) total *= s;
                    if (tensor->dtype == dl::DATA_TYPE_INT8) {
                        int8_t *data = (int8_t *)tensor->data;
                        int8_t max_val = -128, min_val = 127;
                        for (int i = 0; i < total; i++) {
                            if (data[i] > max_val) max_val = data[i];
                            if (data[i] < min_val) min_val = data[i];
                        }
                        ESP_LOGI(TAG_HFD, "  %s: shape=[%d,%d,%d,%d] exp=%d raw=[%d..%d] total=%d",
                                 name,
                                 (int)tensor->shape[0], (int)tensor->shape[1],
                                 (int)tensor->shape[2], (int)tensor->shape[3],
                                 (int)tensor->exponent, (int)min_val, (int)max_val, total);
                    }
                }
            }
        }

        for (auto &c : candidates) {
            ESP_LOGI(TAG_HFD, "  candidate: box=[%d,%d,%d,%d] score=%.3f",
                     c.box[0], c.box[1], c.box[2], c.box[3], c.score);
        }
    }
    return m_mnp->run(img, candidates);
}

dl::detect::Detect &MSRMNP::set_score_thr(float score_thr, int idx)
{
    if (idx == 0 || idx == -1) {
        m_msr->set_score_thr(score_thr, 0);
    }
    return *this;
}

dl::detect::Detect &MSRMNP::set_nms_thr(float nms_thr, int idx)
{
    if (idx == 0 || idx == -1) {
        m_msr->set_nms_thr(nms_thr, 0);
    }
    return *this;
}

dl::Model *MSRMNP::get_raw_model(int idx)
{
    if (idx == 0) {
        return m_msr->get_raw_model(0);
    }
    return nullptr;
}

} // namespace human_face_detect

HumanFaceDetect::HumanFaceDetect(const char *sdcard_model_dir, model_type_t model_type)
{
    switch (model_type) {
    case model_type_t::MSRMNP_S8_V1: {
#if CONFIG_HUMAN_FACE_DETECT_MSRMNP_S8_V1
#if !CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD
        m_model =
            new human_face_detect::MSRMNP("human_face_detect_msr_s8_v1.espdl", "human_face_detect_mnp_s8_v1.espdl");
#else
        if (sdcard_model_dir) {
            char msr_dir[128];
            snprintf(msr_dir, sizeof(msr_dir), "%s/human_face_detect_msr_s8_v1.espdl", sdcard_model_dir);
            char mnp_dir[128];
            snprintf(mnp_dir, sizeof(mnp_dir), "%s/human_face_detect_mnp_s8_v1.espdl", sdcard_model_dir);
            m_model = new human_face_detect::MSRMNP(msr_dir, mnp_dir);
        } else {
            ESP_LOGE("human_face_detect", "please pass sdcard mount point as parameter.");
        }
#endif
#else
        // human_face_detect_msrmnp_s8_v1 is not selected in menuconfig
#endif
        break;
    }
    }
}
