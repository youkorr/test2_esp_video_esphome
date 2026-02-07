#include "dl_detect_msr_postprocessor.hpp"
#include "dl_math.hpp"
#include "esp_log.h"
#include <algorithm>
#include <cmath>

static const char *MSR_PP_TAG = "MSR_PP";

namespace dl {
namespace detect {
template <typename T>
void MSRPostprocessor::parse_stage(TensorBase *score, TensorBase *box, const int stage_index)
{
    int stride_y = m_stages[stage_index].stride_y;
    int stride_x = m_stages[stage_index].stride_x;

    int offset_y = m_stages[stage_index].offset_y;
    int offset_x = m_stages[stage_index].offset_x;

    std::vector<std::vector<int>> &anchor_shape = m_stages[stage_index].anchor_shape;

    int H = score->shape[1];
    int W = score->shape[2];
    int A = anchor_shape.size();
    int C = score->shape[3] / A;
    T *score_ptr = (T *)score->data;
    T *box_ptr = (T *)box->data;
    float score_exp = DL_SCALE(score->exponent);
    float box_exp = DL_SCALE(box->exponent);
    T score_thr_quant = quantize<T>(dl::math::inverse_sigmoid(m_score_thr), 1.f / score_exp);
    float inv_resize_scale_x = m_image_preprocessor->get_resize_scale_x(true);
    float inv_resize_scale_y = m_image_preprocessor->get_resize_scale_y(true);

    for (size_t y = 0; y < H; y++) // height
    {
        for (size_t x = 0; x < W; x++) // width
        {
            for (size_t a = 0; a < A; a++) // anchor number
            {
                for (size_t c = 0; c < C; c++) // category number
                {
                    if (*score_ptr > score_thr_quant) {
                        int center_y = y * stride_y + offset_y;
                        int center_x = x * stride_x + offset_x;
                        int anchor_h = anchor_shape[a][0];
                        int anchor_w = anchor_shape[a][1];
                        result_t new_box = {
                            (int)c,
                            dl::math::sigmoid(dequantize(*score_ptr, score_exp)),
                            {(int)((center_x - (anchor_w >> 1) + anchor_w * dequantize(box_ptr[0], box_exp)) *
                                   inv_resize_scale_x),
                             (int)((center_y - (anchor_h >> 1) + anchor_h * dequantize(box_ptr[1], box_exp)) *
                                   inv_resize_scale_y),
                             (int)((center_x + anchor_w - (anchor_w >> 1) +
                                    anchor_w * dequantize(box_ptr[2], box_exp)) *
                                   inv_resize_scale_x),
                             (int)((center_y + anchor_h - (anchor_h >> 1) +
                                    anchor_h * dequantize(box_ptr[3], box_exp)) *
                                   inv_resize_scale_y)},
                            {}};

                        m_box_list.insert(std::upper_bound(m_box_list.begin(), m_box_list.end(), new_box, greater_box),
                                          new_box);
                    }
                    score_ptr++;
                    box_ptr += 4;
                }
            }
        }
    }
}

template void MSRPostprocessor::parse_stage<int8_t>(TensorBase *score, TensorBase *box, const int stage_index);
template void MSRPostprocessor::parse_stage<int16_t>(TensorBase *score, TensorBase *box, const int stage_index);

void MSRPostprocessor::postprocess()
{
    static uint32_t pp_call_count = 0;
    pp_call_count++;

    TensorBase *score0 = m_model->get_output("score0");
    TensorBase *bbox0 = m_model->get_output("box0");
    TensorBase *score1 = m_model->get_output("score1");
    TensorBase *bbox1 = m_model->get_output("box1");

    // DIAG: Check if output tensors exist and inspect scores
    if (pp_call_count <= 3) {
        ESP_LOGI(MSR_PP_TAG, "DIAG: score0=%p, box0=%p, score1=%p, box1=%p",
                 score0, bbox0, score1, bbox1);

        if (score0 && score0->data) {
            auto s = score0->get_shape();
            ESP_LOGI(MSR_PP_TAG, "score0: shape=[%d,%d,%d,%d], exp=%d, dtype=%d",
                     s.size()>0?s[0]:0, s.size()>1?s[1]:0, s.size()>2?s[2]:0, s.size()>3?s[3]:0,
                     score0->exponent, (int)score0->dtype);

            // Find max score value in score0
            if (score0->dtype == DATA_TYPE_INT8) {
                int8_t *data = (int8_t *)score0->data;
                int total = score0->get_size();
                int8_t max_val = -128;
                float score_exp = DL_SCALE(score0->exponent);
                int8_t thr_quant = quantize<int8_t>(dl::math::inverse_sigmoid(m_score_thr), 1.f / score_exp);
                for (int i = 0; i < total; i++) {
                    if (data[i] > max_val) max_val = data[i];
                }
                float max_score = dl::math::sigmoid(dequantize(max_val, score_exp));
                ESP_LOGI(MSR_PP_TAG, "score0: max_raw=%d, max_score=%.4f, thr_quant=%d, score_thr=%.2f",
                         max_val, max_score, thr_quant, m_score_thr);
            }
        }

        if (score1 && score1->data) {
            auto s = score1->get_shape();
            ESP_LOGI(MSR_PP_TAG, "score1: shape=[%d,%d,%d,%d], exp=%d",
                     s.size()>0?s[0]:0, s.size()>1?s[1]:0, s.size()>2?s[2]:0, s.size()>3?s[3]:0,
                     score1->exponent);

            if (score1->dtype == DATA_TYPE_INT8) {
                int8_t *data = (int8_t *)score1->data;
                int total = score1->get_size();
                int8_t max_val = -128;
                float score_exp = DL_SCALE(score1->exponent);
                for (int i = 0; i < total; i++) {
                    if (data[i] > max_val) max_val = data[i];
                }
                float max_score = dl::math::sigmoid(dequantize(max_val, score_exp));
                ESP_LOGI(MSR_PP_TAG, "score1: max_raw=%d, max_score=%.4f",
                         max_val, max_score);
            }
        }

        if (!score0 || !bbox0 || !score1 || !bbox1) {
            ESP_LOGE(MSR_PP_TAG, "CRITICAL: Missing output tensors! Listing available:");
            auto &outputs = m_model->get_outputs();
            for (auto &[name, tensor] : outputs) {
                ESP_LOGE(MSR_PP_TAG, "  output: '%s'", name.c_str());
            }
        }
    }

    if (score0->dtype == DATA_TYPE_INT8) {
        parse_stage<int8_t>(score0, bbox0, 0);
        parse_stage<int8_t>(score1, bbox1, 1);
    } else {
        parse_stage<int16_t>(score0, bbox0, 0);
        parse_stage<int16_t>(score1, bbox1, 1);
    }
    nms();

    if (pp_call_count <= 3) {
        ESP_LOGI(MSR_PP_TAG, "DIAG: after parse+nms: %d boxes in m_box_list",
                 (int)m_box_list.size());
    }
}
} // namespace detect
} // namespace dl
