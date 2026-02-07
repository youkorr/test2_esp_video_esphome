#include "human_face_detect.hpp"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *DIAG_TAG = "face_detect_diag";

#if CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA
extern const uint8_t human_face_detect_espdl[] asm("_binary_human_face_detect_espdl_start");
static const char *path = (const char *)human_face_detect_espdl;
#elif CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_PARTITION
static const char *path = "human_face_det";
#endif
namespace human_face_detect {

MSR::MSR(const char *model_name)
{
    ESP_LOGI(DIAG_TAG, "MSR: Creating model '%s'...", model_name);
    ESP_LOGI(DIAG_TAG, "MSR: Free heap: internal=%u, PSRAM=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

#if !CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD
    m_model = new dl::Model(
        path, model_name, static_cast<fbs::model_location_type_t>(CONFIG_HUMAN_FACE_DETECT_MODEL_LOCATION));
#else
    m_model =
        new dl::Model(model_name, static_cast<fbs::model_location_type_t>(CONFIG_HUMAN_FACE_DETECT_MODEL_LOCATION));
#endif

    // DIAG: Log model input/output tensor shapes
    if (m_model) {
        auto *input = m_model->get_input();
        if (input) {
            auto shape = input->get_shape();
            if (shape.size() == 4) {
                ESP_LOGI(DIAG_TAG, "MSR input tensor: [%d, %d, %d, %d] (N,H,W,C)",
                         shape[0], shape[1], shape[2], shape[3]);
            } else {
                ESP_LOGI(DIAG_TAG, "MSR input tensor: %d dims", (int)shape.size());
            }
            ESP_LOGI(DIAG_TAG, "MSR input dtype=%d, exponent=%d",
                     (int)input->get_dtype(), input->exponent);
        } else {
            ESP_LOGE(DIAG_TAG, "MSR: get_input() returned null!");
        }
        auto &outputs = m_model->get_outputs();
        ESP_LOGI(DIAG_TAG, "MSR: %d output tensors", (int)outputs.size());
        for (auto &[name, tensor] : outputs) {
            auto s = tensor->get_shape();
            if (s.size() == 4) {
                ESP_LOGI(DIAG_TAG, "MSR output '%s': [%d,%d,%d,%d]",
                         name.c_str(), s[0], s[1], s[2], s[3]);
            } else if (s.size() == 3) {
                ESP_LOGI(DIAG_TAG, "MSR output '%s': [%d,%d,%d]",
                         name.c_str(), s[0], s[1], s[2]);
            }
        }
    } else {
        ESP_LOGE(DIAG_TAG, "MSR: m_model is null after creation!");
    }

    m_image_preprocessor = new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {1, 1, 1}, dl::image::DL_IMAGE_CAP_RGB_SWAP);

    m_postprocessor = new dl::detect::MSRPostprocessor(
        m_model, m_image_preprocessor, 0.2, 0.5, 10, {{8, 8, 9, 9, {{16, 16}, {32, 32}}}, {16, 16, 9, 9, {{64, 64}, {128, 128}}}});

    ESP_LOGI(DIAG_TAG, "MSR: Free heap after init: internal=%u, PSRAM=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

MNP::MNP(const char *model_name)
{
    ESP_LOGI(DIAG_TAG, "MNP: Creating model '%s'...", model_name);

#if !CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD
    m_model = new dl::Model(
        path, model_name, static_cast<fbs::model_location_type_t>(CONFIG_HUMAN_FACE_DETECT_MODEL_LOCATION));
#else
    m_model =
        new dl::Model(model_name, static_cast<fbs::model_location_type_t>(CONFIG_HUMAN_FACE_DETECT_MODEL_LOCATION));
#endif

    // DIAG: Log model input tensor shape
    if (m_model) {
        auto *input = m_model->get_input();
        if (input) {
            auto shape = input->get_shape();
            if (shape.size() == 4) {
                ESP_LOGI(DIAG_TAG, "MNP input tensor: [%d, %d, %d, %d] (N,H,W,C)",
                         shape[0], shape[1], shape[2], shape[3]);
            }
        }
    }

    m_image_preprocessor = new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {1, 1, 1}, dl::image::DL_IMAGE_CAP_RGB_SWAP);
    m_postprocessor = new dl::detect::MNPPostprocessor(m_model, m_image_preprocessor, 0.2, 0.5, 10, {{1, 1, 0, 0, {{48, 48}}}});

    ESP_LOGI(DIAG_TAG, "MNP: initialized OK");
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
    static uint32_t mnp_call_count = 0;
    mnp_call_count++;

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

    if (mnp_call_count <= 3) {
        ESP_LOGI(DIAG_TAG, "MNP: %d candidates in -> %d results out",
                 (int)candidates.size(), (int)result.size());
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
    static uint32_t run_count = 0;
    static uint32_t last_diag_time = 0;
    run_count++;

    std::list<dl::detect::result_t> &candidates = m_msr->run(img);

    // DIAG: Log MSR candidates (first 5 calls, then every 10 seconds)
    uint32_t now = esp_log_timestamp();
    if (run_count <= 5 || (now - last_diag_time > 10000)) {
        last_diag_time = now;
        ESP_LOGI(DIAG_TAG, "MSRMNP run #%u: img=%dx%d, MSR candidates=%d",
                 run_count, img.width, img.height, (int)candidates.size());
        // Log first few candidates with scores
        int i = 0;
        for (auto &c : candidates) {
            if (i >= 5) break;
            ESP_LOGI(DIAG_TAG, "  candidate[%d]: box=[%d,%d,%d,%d] score=%.4f",
                     i, c.box[0], c.box[1], c.box[2], c.box[3], c.score);
            i++;
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
    ESP_LOGI("human_face_detect", "Constructor called: model_type=%d", (int)model_type);
    switch (model_type) {
    case model_type_t::MSRMNP_S8_V1: {
#if CONFIG_HUMAN_FACE_DETECT_MSRMNP_S8_V1
#if !CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD
        ESP_LOGI("human_face_detect", "Loading MSRMNP models from flash rodata...");
        m_model =
            new human_face_detect::MSRMNP("human_face_detect_msr_s8_v1.espdl", "human_face_detect_mnp_s8_v1.espdl");
        if (m_model) {
            ESP_LOGI("human_face_detect", "✅ MSRMNP model loaded successfully!");
        } else {
            ESP_LOGE("human_face_detect", "❌ Failed to create MSRMNP model!");
        }
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
        ESP_LOGE("human_face_detect", "human_face_detect_msrmnp_s8_v1 is not selected in menuconfig.");
#endif
        break;
    }
    }
}
