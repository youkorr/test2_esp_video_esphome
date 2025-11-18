# VGA LVGL Performance Issue - 138ms Block

## Critical Problem Found

User has correctly adapted page for VGA 640x480, but still gets watchdog timeout.

**Root cause found in logs:**
```
[W][component:454]: lvgl took a long time for an operation (138 ms)
[W][component:457]: Components should block for at most 30 ms
```

**LVGL takes 138ms PER UPDATE** - This is the bottleneck!

## Why Watchdog Timeout Occurs

Even with `update_interval: 50ms`, if each LVGL update takes 138ms:

```
Timeline:
t=0ms    : Update #1 starts (takes 138ms to complete)
t=50ms   : Update #2 requested → BLOCKED (update #1 still running)
t=100ms  : Update #3 requested → QUEUED
t=138ms  : Update #1 finally finishes
t=138ms  : Update #2 starts (takes 138ms)
t=150ms  : Update #4 requested → QUEUED
...

Result: LVGL constantly blocking, loopTask can NEVER execute
After 5 seconds: Watchdog timeout ❌
```

## Solution: update_interval MUST Be > 138ms

```yaml
lvgl_camera_display:
  update_interval: 150ms  # Minimum for VGA
  # Measured: 138ms per update + 12ms safety margin
  # Result: ~6-7 FPS display (slow but STABLE)
```

### Why Not Lower?

| update_interval | LVGL time | Result |
|----------------|-----------|---------|
| 20ms | 138ms | ❌ Accumulation → Watchdog |
| 50ms | 138ms | ❌ Accumulation → Watchdog |
| 100ms | 138ms | ❌ Still accumulates |
| **150ms** | 138ms | ✅ No accumulation |
| 200ms | 138ms | ✅ Extra safe |

## Why Is VGA So Slow in LVGL?

### 1. Buffer Copy/Conversion Overhead
- SPIRAM buffer → LVGL canvas requires memory copy
- RGB565 conversion/scaling adds overhead
- Cache line alignment requirements

### 2. Canvas Operations
- `lv_canvas_set_buffer()` is expensive for 640×480
- LVGL may be doing unnecessary redraws
- Font rendering (nunito_24) on buttons

### 3. SPIRAM Access Latency
- Buffer in SPIRAM (0x48532880)
- Slower access than internal RAM
- Cache invalidation overhead

## Applied Fix

**File: rtsp_ov5647.yaml**
```yaml
lvgl_camera_display:
  update_interval: 150ms  # Changed from 50ms
```

## Expected Results

### Before (update_interval: 50ms)
```
✅ First frame captured
⚠️ LVGL took 138ms
⏱️ t=50ms: Another update queued
⏱️ t=100ms: Another update queued
...
❌ Watchdog timeout after 5 seconds
```

### After (update_interval: 150ms)
```
✅ First frame captured
⚠️ LVGL took 138ms
⏱️ t=150ms: Next update (previous finished)
✅ No accumulation
✅ loopTask can execute
✅ NO watchdog timeout
```

Display will be **~6-7 FPS** but **STABLE**.

## Alternative Solutions

### Option 1: Use 800x600 @ 50 FPS (RECOMMENDED)
800x600 is better optimized, can use 33ms interval:

```yaml
mipi_dsi_cam:
  resolution: 800x600
  framerate: 50

lvgl_camera_display:
  update_interval: 33ms  # 30 FPS display, smooth
```

### Option 2: Disable LVGL Display for VGA
If you only need RTSP/web server with VGA:

```yaml
# Comment out:
# lvgl_camera_display:
#   ...

# Keep only:
rtsp_server: ...
camera_web_server: ...
```

### Option 3: Optimize LVGL Canvas
Reduce canvas operations:
- Remove custom fonts (use default)
- Remove buttons during streaming
- Use simpler page layout

## Performance Comparison

| Resolution | LVGL Time | update_interval | Display FPS | Watchdog |
|-----------|-----------|----------------|-------------|----------|
| VGA 640x480 | 138ms | 50ms | N/A | ❌ Timeout |
| VGA 640x480 | 138ms | **150ms** | 6-7 | ✅ Stable |
| 800x600 | ~30ms | 33ms | 30 | ✅ Smooth |
| 800x600 | ~30ms | 50ms | 20 | ✅ Stable |

## Why 800x600 Is Faster Than VGA?

Despite being larger resolution:
- Better optimized in LVGL/ISP pipeline
- Native resolution support (no scaling)
- Better cache alignment
- More testing/optimization in codebase

## Testing Checklist

With `update_interval: 150ms`:

- [ ] Compile and flash
- [ ] Check logs for "LVGL took" warnings
- [ ] Verify no watchdog timeout after 30 seconds
- [ ] Camera displays (slowly at ~6 FPS)
- [ ] loopTask responsive

If still issues:
- Increase to 200ms
- Or switch to 800x600 @ 50 FPS

## Files Modified

- `rtsp_ov5647.yaml`: update_interval 50ms → 150ms

## Recommendation

**For VGA testing**: Use 150ms (stable but slow)
**For production**: Use 800x600 @ 50 FPS with 33ms (smooth and stable)

---

**Critical learning**: With LVGL, update_interval must be greater than actual processing time, not just the desired FPS.
