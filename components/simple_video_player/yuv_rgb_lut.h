#pragma once

#include <cstdint>

// Initialize lookup tables (call once at startup)
void init_yuv_lut_tables();

// Convert I420 (YUV420 planar) to RGB565 using lookup tables
// Optimized for speed: ~10-15ms for 480x272 (vs 17-19ms SIMD)
void yuv420_to_rgb565_lut(const uint8_t *yuv, uint8_t *rgb, int width, int height);
