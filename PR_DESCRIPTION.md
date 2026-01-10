# Fix tearing issue in LVGL camera display + critical buffer bugs

## Summary
This PR fixes the tearing issue (image split in half) in LVGL camera display and 2 critical bugs in buffer pointer management.

## Problems Fixed

### 1. 🐛 Tearing Issue (Image Cut in Half)

**Problem:** Buffer was re-queued to V4L2 immediately after DQBUF, causing V4L2 DMA to write new frame while LVGL was still displaying it.

**Root Cause:**
```cpp
// ❌ BUG: Immediate requeue in capture_frame()
VIDIOC_DQBUF → buffer[0]
VIDIOC_QBUF → buffer[0]  // ← RE-QUEUED IMMEDIATELY!
lv_canvas_set_buffer(buffer[0])
→ V4L2 DMA writes new frame WHILE LVGL reads → TEARING!
```

**Solution:**
- Added `pending_release_buffers_[]` queue to delay buffer requeue
- Modified `capture_frame()` to requeue pending buffers BEFORE DQBUF
- Modified `release_buffer()` to add buffers to pending queue

### 2. 🐛 Critical Bug: PPA Corruption

**Problem:** Reading `data` instead of `v4l2_data` caused corruption when PPA was active.

**Fix:** Always read from `v4l2_data` (where V4L2 DMA writes)

### 3. 🐛 Critical Bug: Frozen Frames After PPA Toggle

**Problem:** `data` pointer not restored when PPA disabled, causing stale frames.

**Fix:** Always restore `data = v4l2_data` before conditionally overriding it.

## Architecture Verified ✅

- **ISP Processing** (/dev/video20): AWB, CCM, AE, RAW→RGB565
- **DMA Transfer** (/dev/video0): V4L2 USERPTR zero-copy to SPIRAM
- **LVGL Rendering**: lv_canvas_set_buffer() zero-copy display

## Changes

### Files Modified
- `esp_cam_sensor_camera.h`: Added v4l2_data, pending_release_buffers_[]
- `esp_cam_sensor_camera.cpp`: Fixed buffer management in capture/release

### Key Improvements
- Pending queue prevents tearing (buffer protected until LVGL done)
- v4l2_data usage fixes PPA corruption
- Data pointer restoration fixes PPA toggle bug
- Index validation prevents crashes

## Testing Recommended

1. **Without PPA**: Verify no tearing at 30 FPS
2. **With PPA**: Verify rotation/mirror works without corruption
3. **Toggle PPA**: Verify image doesn't freeze
4. **High FPS**: Verify no tearing at 50 FPS
5. **With detection**: Verify overlays work correctly

## Documentation

See `VERIFICATION_TEARING_FIX.md` for complete technical details and verification checklist.

## Commits

- bd8f28e: Fix tearing issue (pending queue)
- 6be90f2: Fix critical bugs (PPA compatibility)
- 7ce2baa: Add verification document
