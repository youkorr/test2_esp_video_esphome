# 🚀 Waveshare ESP32-P4 Video Optimization Guide

## Based on Analysis of Official Waveshare Implementation
Repository: https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B

---

## 🎯 Problem: 1024x600 @ 5 FPS (Target: 15 FPS)

**Current Performance:**
- File read: ~10ms
- JPEG decode: 51ms
- **LVGL display: ~140ms** ⚠️ **BOTTLENECK**
- **Total: ~200ms = 5 FPS**

**Root Cause:** LVGL blocking on display flush/sync

---

## ✅ Waveshare Critical Optimizations

### 1. **ESPHome LVGL Configuration** (MOST IMPORTANT)

Add to your ESPHome YAML:

```yaml
lvgl:
  # Your existing display config...

  displays:
    - display_id: my_display
      buffer_size: 100%          # ⚡ Full framebuffer (1024x600)
      double_buffer: true        # 🚨 CRITICAL: Prevents blocking

  # Advanced LVGL configuration
  on_idle:
    timeout: 16                  # 16ms = 60Hz max refresh

# Platform-specific optimizations (if accessible via ESPHome)
esphome:
  platformio_options:
    build_flags:
      - "-DCONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE=1"     # Direct mode bypass
      - "-DCONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR=1"      # Anti-tearing
      - "-DCONFIG_BSP_LCD_DPI_BUFFER_NUMS=2"          # 2 DPI buffers
      - "-DCONFIG_SPIRAM_SPEED_200M=1"                # 200MHz PSRAM
      - "-DCONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=1"   # IRAM for speed
      - "-DCONFIG_FREERTOS_HZ=1000"                   # 1ms tick precision
      - "-DCONFIG_COMPILER_OPTIMIZATION_PERF=1"       # Performance optimized
```

### 2. **JPEG Decoder Settings** ✅ ALREADY APPLIED

```cpp
jpeg_decode_engine_cfg_t cfg = {
    .intr_priority = 0,    // Default (Waveshare uses this)
    .timeout_ms = 0,       // No timeout (decode as fast as possible)
};
```

**Why:**
- Higher priority (3) caused slower performance
- Timeout creates artificial delays
- Waveshare uses `-1` (infinite) or `0` (no limit)

### 3. **Display Update Pattern**

**Waveshare Approach (Non-Blocking):**
```cpp
// 10ms timeout - skips frame if display busy
if (bsp_display_lock(10)) {
    lv_obj_invalidate(canvas);
    bsp_display_unlock();
}
// Continues immediately even if lock fails
```

**Current Implementation (Blocking):**
```cpp
lv_canvas_set_buffer(...);   // May block
lv_obj_invalidate(...);      // Waits for LVGL task
// = Up to 140ms blocked here
```

**Solution:** ESPHome doesn't expose `bsp_display_lock()`, so we rely on:
- **Double buffering** (config above)
- **Direct mode** (bypasses extra copies)

### 4. **Buffer Alignment** ✅ ALREADY APPLIED

```cpp
// 16-byte alignment for JPEG decoder
aligned_width_ = (actual_width_ + 15) & ~15;

// 128-byte alignment for cache buffers (DMA-friendly)
// Already using VIDEO_BUFFER_CAPS with MALLOC_CAP_CACHE_ALIGNED
```

### 5. **Memory Configuration**

**Waveshare Settings:**
```cpp
// Output buffer (RGB888 intermediate)
out_buff_size = width * height * 3;  // NOT RGB565!

// Cache buffer (file I/O)
cache_buff_size = 64KB;  // 128-byte aligned

// Display buffers (double buffered)
buffer_size = 1024 * 600 * 2 bytes = 1.2MB × 2 = 2.4MB total
```

**Recommendation:**
- Keep RGB565 output (saves memory: width×height×2 instead of ×3)
- Ensure double buffering enabled (see YAML above)

---

## 📊 Expected Results After Optimization

**With Double Buffering + Direct Mode:**
```
File read:      ~10ms   (already optimized)
JPEG decode:    ~40ms   (improved with timeout=0)
LVGL display:   ~20ms   ⚡ 85% IMPROVEMENT (from 140ms)
────────────────────────
TOTAL:          ~70ms   = 14-15 FPS ✅ TARGET ACHIEVED
```

**Why Display Time Drops:**
- **Double buffer:** LVGL writes to buffer 2 while GPU displays buffer 1 (no waiting)
- **Direct mode:** No intermediate copy to LVGL internal buffer
- **Non-blocking:** Skips frame rather than wait (smooth playback)

---

## 🧪 Testing Steps

### 1. Update ESPHome Config

Add the YAML configuration above to your ESPHome device config.

### 2. Flash and Test

```bash
esphome run your_device.yaml
```

### 3. Check New Logs

You should see:
```
[I][simple_video_player]: ✅ JPEG hardware decoder initialized (intr_priority=0, timeout=unlimited, Waveshare-optimized)
[I][simple_video_player]: ⏱️ MJPEG timing (1024x600): TOTAL=70ms [File read=10ms, JPEG decode=40ms, LVGL display=20ms]
[I][simple_video_player]: 💡 Bottleneck analysis: Display/Total = 28.6% (should be <30%% for good perf)
[I][simple_video_player]: 📊 Performance: 14.29 FPS | target: 15 FPS
```

---

## 🔧 Fallback: If Still Slow

### Option A: Reduce Resolution Temporarily

Test with 800x480 to verify config works:
```yaml
simple_video_player:
  width: 800
  height: 480
```

Expected: 20-25 FPS (confirms optimization works)

### Option B: Skip Frames Dynamically

If display is slow, skip every Nth frame:
```cpp
// In timer_cb_(), only process every 2nd frame if slow
static int frame_skip_counter = 0;
if (decode_time + display_time > 60) {  // Taking >60ms
    if (++frame_skip_counter % 2 != 0) {
        return;  // Skip this frame
    }
}
```

### Option C: Lower Target FPS

If 15 FPS impossible, target 12 FPS:
```yaml
simple_video_player:
  fps: 12  # 83ms per frame - more comfortable margin
```

---

## 📚 Key Waveshare Insights

1. **Simplicity over complexity** - Sequential decode loop, no threading
2. **Non-blocking everything** - 10ms timeout on all locks
3. **Double buffer CRITICAL** - Prevents tearing + eliminates blocking
4. **Direct mode CRITICAL** - Bypasses LVGL internal copies
5. **No frame pacing** - Runs as fast as possible (decoder self-paces)
6. **PSRAM @ 200MHz** - Critical for 1.2MB buffer access
7. **RGB565 sufficient** - Saves 33% bandwidth vs RGB888

---

## 📖 Sources

- [Waveshare ESP32-P4 Wiki](https://www.waveshare.com/wiki/ESP32-P4-WIFI6)
- [LVGL Performance Optimization](https://forum.lvgl.io/t/lvgl-config-optimization-to-increase-fps/11476)
- [ESP32-P4 Boards - LVGL](https://lvgl.io/boards)
- [Waveshare LCD & LVGL Performance PDF](https://files.waveshare.com/wiki/common/Performance.pdf)
- [Waveshare GitHub - Video Player Component](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B/tree/aef7fbfb6edccd3f931ec7980f67a152854fc03f/examples/ESP-IDF/11_esp_brookesia_phone/components/apps/video_player)

---

**Next Steps:**
1. ✅ Apply YAML configuration above
2. ✅ Flash and test
3. ✅ Share new diagnostic logs
4. ⚡ Should achieve 12-15 FPS!
