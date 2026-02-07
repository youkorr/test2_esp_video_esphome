#include "dl_detect_base.hpp"
#include "esp_log.h"

static const char *DIMPL_TAG = "DetectImpl";

namespace dl {
namespace detect {
DetectWrapper::~DetectWrapper()
{
    delete m_model;
}

std::list<dl::detect::result_t> &DetectWrapper::run(const dl::image::img_t &img)
{
    if (!m_model) {
        load_model();
    }
    return m_model->run(img);
}

Detect &DetectWrapper::set_score_thr(float score_thr, int idx)
{
    assert(idx == 0 || idx == 1);
    if (m_model) {
        m_model->set_score_thr(score_thr, idx);
    } else {
        m_score_thr[idx] = score_thr;
    }
    return *this;
}

Detect &DetectWrapper::set_nms_thr(float nms_thr, int idx)
{
    assert(idx == 0 || idx == 1);
    if (m_model) {
        m_model->set_nms_thr(nms_thr, idx);
    } else {
        m_nms_thr[idx] = nms_thr;
    }
    return *this;
}

dl::Model *DetectWrapper::get_raw_model(int idx)
{
    assert(idx == 0 || idx == 1);
    if (!m_model) {
        load_model();
    }
    return m_model->get_raw_model(idx);
}

DetectImpl::~DetectImpl()
{
    delete m_model;
    delete m_image_preprocessor;
    delete m_postprocessor;
}

std::list<dl::detect::result_t> &DetectImpl::run(const dl::image::img_t &img)
{
    static uint32_t impl_run_count = 0;
    impl_run_count++;

    DL_LOG_INFER_LATENCY_INIT();
    DL_LOG_INFER_LATENCY_START();
    m_image_preprocessor->preprocess(img);
    DL_LOG_INFER_LATENCY_END_PRINT("detect", "pre");

    DL_LOG_INFER_LATENCY_START();
    m_model->run();
    DL_LOG_INFER_LATENCY_END_PRINT("detect", "model");

    // DIAG: Inspect model output tensors on first few runs
    if (impl_run_count <= 3) {
        auto &outputs = m_model->get_outputs();
        for (auto &[name, tensor] : outputs) {
            auto shape = tensor->get_shape();
            int total_elements = tensor->get_size();
            ESP_LOGI(DIMPL_TAG, "DIAG output '%s': dims=%d, elements=%d, dtype=%d",
                     name.c_str(), (int)shape.size(), total_elements, (int)tensor->get_dtype());

            // Check if output contains any non-zero values (detect dead model)
            if (tensor->data && total_elements > 0) {
                int8_t *data = (int8_t *)tensor->data;
                int non_zero = 0;
                int8_t max_val = -128, min_val = 127;
                for (int i = 0; i < total_elements && i < 1000; i++) {
                    if (data[i] != 0) non_zero++;
                    if (data[i] > max_val) max_val = data[i];
                    if (data[i] < min_val) min_val = data[i];
                }
                ESP_LOGI(DIMPL_TAG, "DIAG output '%s': non_zero=%d/%d, range=[%d,%d]",
                         name.c_str(), non_zero, std::min(total_elements, 1000), min_val, max_val);
            }
        }
    }

    DL_LOG_INFER_LATENCY_START();
    m_postprocessor->clear_result();
    m_postprocessor->postprocess();
    std::list<dl::detect::result_t> &result = m_postprocessor->get_result(img.width, img.height);
    DL_LOG_INFER_LATENCY_END_PRINT("detect", "post");

    if (impl_run_count <= 3) {
        ESP_LOGI(DIMPL_TAG, "DIAG: DetectImpl::run() on %dx%d -> %d results",
                 img.width, img.height, (int)result.size());
    }

    return result;
}

Detect &DetectImpl::set_score_thr(float score_thr, int idx)
{
    m_postprocessor->set_score_thr(score_thr);
    return *this;
}

Detect &DetectImpl::set_nms_thr(float nms_thr, int idx)
{
    m_postprocessor->set_nms_thr(nms_thr);
    return *this;
}

dl::Model *DetectImpl::get_raw_model(int idx)
{
    return m_model;
}
} // namespace detect
} // namespace dl
