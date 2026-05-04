// Self-contained copy of dl_base_dotprod implementations without ESP-DSP
// dependency. Each detection component (face_detection / pedestrian_detection /
// yolo11_detection) ships its own copy so users can pick any subset of them
// in their YAML's external_components list without the linker failing.
//
// All function definitions are marked __attribute__((weak)) so the linker
// will keep only one copy when several components are linked together.

#include "dl_base_dotprod.hpp"
#include "dl_base_isa.hpp"
#include "dl_tool.hpp"

namespace dl {
namespace base {

// C reference implementation for int8_t
__attribute__((weak)) void dotprod_c(int8_t *input0_ptr, int8_t *input1_ptr,
                                     int16_t *output_ptr, int length, int shift)
{
    int32_t result = 0;
    float scale = DL_RESCALE(shift);
    for (int i = 0; i < length; i++) {
        result += (int32_t)input0_ptr[i] * (int32_t)input1_ptr[i];
    }
    dl::tool::truncate(*output_ptr, tool::round(result * scale));
}

// C reference implementation for int8_t * int16_t
__attribute__((weak)) void dotprod_c(int8_t *input0_ptr, int16_t *input1_ptr,
                                     int16_t *output_ptr, int length, int shift)
{
    int32_t result = 0;
    float scale = DL_RESCALE(shift);
    for (int i = 0; i < length; i++) {
        result += (int32_t)input0_ptr[i] * (int32_t)input1_ptr[i];
    }
    dl::tool::truncate(*output_ptr, tool::round(result * scale));
}

// C reference implementation for int16_t
__attribute__((weak)) void dotprod_c(int16_t *input0_ptr, int16_t *input1_ptr,
                                     int16_t *output_ptr, int length, int shift)
{
    int64_t result = 0;
    float scale = DL_RESCALE(shift);
    for (int i = 0; i < length; i++) {
        result += (int32_t)input0_ptr[i] * (int32_t)input1_ptr[i];
    }
    dl::tool::truncate(*output_ptr, tool::round(result * scale));
}

// Optimized version for int8_t (uses ESP32-P4 SIMD if available)
__attribute__((weak)) void dotprod(int8_t *input0_ptr, int8_t *input1_ptr,
                                   int16_t *output_ptr, int length, int shift)
{
    if (length % 16 == 0 && shift >= 0) {
#if CONFIG_ESP32P4_BOOST
        dl_esp32p4_cfg_round(ROUND_MODE_HALF_EVEN);
        dl_esp32p4_dotprod_i8k8o16(output_ptr, input0_ptr, input1_ptr, shift, length);
#else
        dotprod_c(input0_ptr, input1_ptr, output_ptr, length, shift);
#endif
    } else {
        dotprod_c(input0_ptr, input1_ptr, output_ptr, length, shift);
    }
}

// Optimized version for int8_t * int16_t
__attribute__((weak)) void dotprod(int8_t *input0_ptr, int16_t *input1_ptr,
                                   int16_t *output_ptr, int length, int shift)
{
    if (length % 8 == 0 && shift >= 0) {
#if CONFIG_ESP32P4_BOOST
        dl_esp32p4_cfg_round(ROUND_MODE_HALF_EVEN);
        dl_esp32p4_dotprod_i16k8o16(output_ptr, input0_ptr, input1_ptr, shift, length);
#else
        dotprod_c(input0_ptr, input1_ptr, output_ptr, length, shift);
#endif
    } else {
        dotprod_c(input0_ptr, input1_ptr, output_ptr, length, shift);
    }
}

// Optimized version for int16_t
__attribute__((weak)) void dotprod(int16_t *input0_ptr, int16_t *input1_ptr,
                                   int16_t *output_ptr, int length, int shift)
{
    if (length % 8 == 0 && shift >= 0) {
#if CONFIG_ESP32P4_BOOST
        dl_esp32p4_cfg_round(ROUND_MODE_HALF_EVEN);
        dl_esp32p4_dotprod_i16k16o16(output_ptr, input0_ptr, input1_ptr, shift, length);
#else
        dotprod_c(input0_ptr, input1_ptr, output_ptr, length, shift);
#endif
    } else {
        dotprod_c(input0_ptr, input1_ptr, output_ptr, length, shift);
    }
}

// Manual implementation for float (replaces dsps_dotprod_f32 from ESP-DSP)
__attribute__((weak)) void dotprod(float *input0_ptr, float *input1_ptr,
                                   float *output_ptr, int length, int shift)
{
    (void)shift;
    float result = 0.0f;
    for (int i = 0; i < length; i++) {
        result += input0_ptr[i] * input1_ptr[i];
    }
    *output_ptr = result;
}

} // namespace base
} // namespace dl
