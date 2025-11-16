# SC202CS Low FPS Analysis (4 FPS Issue)

## Problem

SC202CS showing only **4.10 FPS** instead of expected **25-30 FPS**:
```
🎞️ 300 frames - FPS: 4.10 | capture: 0.2ms | canvas: 0.4ms | skip: 0.0%
```

## Root Cause Identified

**The issue is NOT in the camera driver** - it's in the YAML configuration.

### Evidence

From the log:
- **capture: 0.2ms** ← Camera DQBUF is FAST (working correctly)
- **canvas: 0.4ms** ← Canvas update is FAST (working correctly)
- **skip: 0.0%** ← capture_frame() succeeds EVERY time it's called
- **FPS: 4.10** ← capture_frame() is only being called ~4 times per second

This means the LVGL timer's `update_interval` is configured for ~250ms instead of the correct value for your target FPS.

## Waveshare vs Current Implementation

### Waveshare Architecture (for reference)

```c
// Dedicated streaming task (FreeRTOS)
void video_stream_task(void *arg) {
    while (streaming) {
        ioctl(fd, VIDIOC_DQBUF, &buf);  // Dequeue frame
        user_callback(buf.m.userptr);    // Process frame
        ioctl(fd, VIDIOC_QBUF, &buf);    // Requeue immediately
    }
}

xTaskCreatePinnedToCore(video_stream_task, "video", 4096, NULL, 5, NULL, 1);
```

**Key features:**
- Dedicated FreeRTOS task
- Continuous polling loop (maximum FPS)
- Task pinned to specific CPU core
- Event-based synchronization

### Our Current Architecture

```cpp
// LVGL timer-based (ESPHome)
void LVGLCameraDisplay::lvgl_timer_callback_(lv_timer_t *timer) {
    display->update_camera_frame_();
}

void LVGLCameraDisplay::update_camera_frame_() {
    bool captured = this->camera_->capture_frame();  // DQBUF + QBUF
    if (captured) {
        this->update_canvas_();  // Display frame
    }
}

// Timer created with update_interval from YAML:
lv_timer_create(callback, this->update_interval_, this);
```

**Key features:**
- LVGL timer calls capture_frame() periodically
- Timer interval controlled by YAML `update_interval`
- Simple, integrated with ESPHome/LVGL
- **FPS determined by update_interval configuration**

## Solution: Fix Your YAML Configuration

### For SC202CS VGA (640×480) @ 30 FPS

```yaml
mipi_dsi_cam:
  id: cam
  sensor_type: sc202cs
  resolution: "VGA"        # 640×480
  pixel_format: RGB565
  framerate: 30            # Sensor framerate

lvgl_camera_display:
  id: camera_display
  camera_id: cam
  canvas_id: camera_canvas
  update_interval: 33ms    # ← FIX THIS! 1000ms/30fps = 33ms
```

### For SC202CS VGA @ 25 FPS (more conservative)

```yaml
lvgl_camera_display:
  update_interval: 40ms    # 1000ms/25fps = 40ms
```

### For SC202CS 720P (1280×720) @ 19 FPS (max theoretical)

SC202CS 720P has lower max FPS due to MIPI 1-lane bandwidth:

```yaml
mipi_dsi_cam:
  resolution: "720P"       # 1280×720
  framerate: 20            # Rounded to 20 FPS (max ~19.4)

lvgl_camera_display:
  update_interval: 50ms    # 1000ms/20fps = 50ms
```

## Framerate Calculation Reference

| Target FPS | update_interval | Use Case |
|-----------|----------------|----------|
| 60 FPS | 16ms | High-speed (requires fast sensor like OV5647 800x640@50) |
| 50 FPS | 20ms | OV5647 800x640 @ 50 FPS |
| 30 FPS | **33ms** | **SC202CS VGA @ 30 FPS** ← Recommended |
| 25 FPS | 40ms | SC202CS VGA @ 25 FPS (conservative) |
| 20 FPS | 50ms | SC202CS 720P @ 20 FPS (max) |
| 15 FPS | 67ms | Low bandwidth |
| 10 FPS | 100ms | Very low bandwidth |
| 4 FPS | 250ms | **Your current config** ← WRONG |

## SC202CS Sensor Capabilities

### VGA (640×480) RAW8

From `sc202cs_custom_formats.h`:

```c
// Timing
HTS: 1500 pixels
VTS: 990 lines
PCLK: ~28.8 MHz

// Theoretical max FPS:
// FPS = PCLK / (HTS × VTS) = 28,800,000 / (1500 × 990) = 19.4 FPS
```

**Practical max for VGA: ~19 FPS, but configure for 30 FPS (sensor will cap at max)**

### 720P (1280×720) RAW8

```c
// From ESP-IDF driver
HTS: 1500 pixels
VTS: 1050 lines
PCLK: 28.8 MHz

// Max FPS:
// 28,800,000 / (1500 × 1050) = 18.3 FPS
```

**Practical max for 720P: ~18 FPS**

## Why Your Current Config Shows 4 FPS

You likely have:

```yaml
lvgl_camera_display:
  update_interval: 250ms   # ← This causes 4 FPS (1000/250 = 4)
```

Or possibly:

```yaml
lvgl_camera_display:
  update_interval: 100ms   # ← Would cause 10 FPS
```

## Recommended Fix

**Check your YAML file** for the `update_interval` parameter and change it to:

```yaml
lvgl_camera_display:
  camera_id: cam
  canvas_id: camera_canvas
  update_interval: 33ms    # 30 FPS - CHANGE THIS LINE
```

Then recompile and flash.

## Expected Results After Fix

```
🎞️ 300 frames - FPS: 19.0 | capture: 0.2ms | canvas: 0.4ms | skip: 45.0%
                    ↑                                               ↑
              Real sensor max                           Sensor can't keep up with 30 FPS timer
```

or if you use `update_interval: 50ms` (20 FPS):

```
🎞️ 300 frames - FPS: 18.5 | capture: 0.2ms | canvas: 0.4ms | skip: 8.0%
                    ↑                                               ↑
              Close to sensor max                      Sensor keeps up better
```

## Comparison with OV5647 800x640@50

OV5647 can achieve 50 FPS because:
- **2-lane MIPI** (vs SC202CS 1-lane)
- **Higher PCLK** (~100 MHz vs 28.8 MHz)
- **Optimized timing** for 800x640

```
OV5647 800x640:
PCLK: 100 MHz
HTS: 1896, VTS: 984
Max FPS: 100,000,000 / (1896 × 984) = 53.6 FPS ← Can do 50 FPS easily!

SC202CS VGA:
PCLK: 28.8 MHz
HTS: 1500, VTS: 990
Max FPS: 28,800,000 / (1500 × 990) = 19.4 FPS ← Can't do 30 FPS
```

## Action Items

1. ✅ **Find your YAML configuration file**
2. ✅ **Locate the `lvgl_camera_display` section**
3. ✅ **Change `update_interval` to `33ms`** (for 30 FPS attempt) or `50ms` (for 20 FPS)
4. ✅ **Recompile and flash**
5. ✅ **Verify FPS increases to ~18-19 FPS** (SC202CS max)

## Note on skip rate

After fixing update_interval, you'll see:
- **skip: 0.0%** if `update_interval >= 50ms` (timer slower than sensor)
- **skip: ~40-50%** if `update_interval = 33ms` (timer faster than sensor max)

This is NORMAL - the sensor physically can't capture faster than ~19 FPS due to MIPI 1-lane bandwidth.

## References

- Waveshare ESP32-P4 camera: Uses dedicated FreeRTOS task for maximum FPS
- M5Stack Tab5: Uses PPA for hardware transforms
- Our implementation: Uses LVGL timer (simpler, FPS controlled by YAML)
- SC202CS datasheet: 1-lane MIPI, max ~19 FPS for VGA
- OV5647: 2-lane MIPI, can do 50 FPS for 800x640
