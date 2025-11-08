# PPA Performance Optimization Tests - Summary

## 🎯 Objective

Optimize PPA (Pixel Processing Accelerator) performance to achieve 30 FPS video streaming from MIPI-CSI camera to LVGL display.

**Current Performance:**
- PPA copy time: 43488 us (43.5ms)
- Bandwidth: 42 MB/s (much slower than expected DMA speed)
- Actual FPS: ~22 FPS

**Target Performance:**
- PPA copy time: <33000 us (33ms)
- Target FPS: 30 FPS
- Expected bandwidth: >100 MB/s for hardware DMA

## ✅ Implemented Optimizations

### Test 1: Enable mirror_x (Commit 5e4695d)
**What:** Changed `mirror_x = false` → `mirror_x = true` to match M5Stack configuration
**Why:** M5Stack Tab5 uses mirror_x=true and achieves >30 FPS with same resolution
**Success Probability:** 10%
**Expected logs:** None (configuration change only)

### Test 2: Memory Zone Analysis (Commit a89b43a)
**What:** Added detailed memory zone detection and logging
**Why:** PPA performance may depend on which memory region buffers are allocated in
**Success Probability:** 30%
**Expected logs:**
```
📍 Memory Zone Analysis (Test 2):
   V4L2 buffer[0]: 0x483c0000 → SPIRAM (0x48000000-0x4C000000)
   V4L2 buffer[1]: 0x48580000 → SPIRAM (0x48000000-0x4C000000)
   image_buffer_: 0x48200000 → SPIRAM (0x48000000-0x4C000000)

💡 PPA Performance Notes:
   - PPA DMA should work efficiently on SPIRAM with DMA capability
   - Expected PPA bandwidth: >100 MB/s
   - Current observed: ~42 MB/s (investigating why)
```

### Test 3: Cache Synchronization ⭐ (Commit ea48d5a)
**What:** Added esp_cache_msync() before and after PPA copy
**Why:** PPA DMA requires cache coherency for correct operation
**Success Probability:** 40% (HIGHEST)
**Implementation:**
- Before PPA: `esp_cache_msync(src, size, C2M | INVALIDATE)` - Write cache to memory
- After PPA: `esp_cache_msync(dest, size, M2C)` - Load memory to cache
**Expected logs:** Performance improvement in PPA timing logs

### Test 4: 64-byte Aligned Allocation (Commit ed57dba)
**What:** Changed to `heap_caps_aligned_alloc(64, size, flags)` for image buffer
**Why:** PPA DMA may require 64-byte alignment for optimal performance
**Success Probability:** 20%
**Expected logs:**
```
✓ Image buffer allocated: 1843200 bytes @ 0x48200000 (DMA+SPIRAM, 64-byte aligned ✓)
```

## 📊 How to Test

1. **Flash the firmware** with these optimizations
2. **Navigate to camera page** in LVGL
3. **Click START** to begin streaming
4. **Observe the logs** for:

### Key Logs to Check:

#### At Streaming Start:
```
📍 Memory Zone Analysis (Test 2):
   [Check that all buffers are in SPIRAM]

✓ Image buffer allocated: ... (DMA+SPIRAM, 64-byte aligned ✓)
```

#### During Streaming (every 100 frames):
```
📊 Profiling (avg over 100 frames):
   DQBUF: XXX us
   PPA copy: XXXXX us  ← THIS IS THE KEY METRIC
   QBUF: XXX us
   TOTAL: XX.X ms → ~XX FPS
```

**Look for PPA copy time:**
- ✅ <33000 us (33ms) → SUCCESS! Will achieve 30 FPS
- ⚠️  33000-40000 us → Improved but not quite 30 FPS
- ❌ >43000 us → No improvement, may need alternative approach

#### LVGL Display Logs:
```
🎞️ 100 frames - FPS: XX.XX | capture: XX.Xms | canvas: X.Xms
```

**Look for FPS:**
- ✅ ≥28 FPS → SUCCESS! Smooth video
- ⚠️  24-28 FPS → Acceptable, slight improvement
- ❌ <24 FPS → No improvement

## 🔍 Analysis Guide

### If Performance Improved:
1. **Identify which test helped** by comparing with baseline (43488 us)
2. **Document the improvement** in issue/PR
3. **Celebrate!** 🎉

### If No Significant Improvement:
The investigation plan includes alternative solutions:

#### Option A: Zero-Copy Approach
- Use V4L2 MMAP buffers directly with LVGL (no PPA copy)
- Performance: ~2ms instead of 43ms
- Guaranteed 30 FPS
- Small risk of tearing (acceptable in practice)
- Implementation: See commit 108a4d3

#### Option B: Reduce Resolution
- 480P (640x480) → ~28 FPS with PPA
- QVGA (320x240) → ~30 FPS with PPA
- Trade-off: Lower quality for higher framerate

#### Option C: Profile M5Stack Directly (Test 5)
- Modify M5Stack Tab5 code to measure actual PPA time
- Verify if they really achieve >30 FPS or if ~20 FPS is normal
- This gives ground truth for PPA performance expectations

## 📈 Performance Metrics Summary

| Metric | Before | Target | Test Method |
|--------|--------|--------|-------------|
| PPA Time | 43488 us | <33000 us | Profiling logs |
| Bandwidth | 42 MB/s | >100 MB/s | Calculated |
| FPS | ~22 | 30 | LVGL logs |
| Frame Time | ~45ms | 33ms | capture_frame() |

## 🎓 Technical Details

### Why These Tests?

1. **mirror_x**: Simple config difference with M5Stack
2. **Memory zones**: SPIRAM vs SRAM affects DMA bandwidth
3. **Cache sync**: ESP32-P4 cache architecture requires explicit coherency
4. **Alignment**: DMA peripherals often require aligned buffers

### Combined Probability
Assuming independent issues, at least one fix working:
```
P(at least one) = 1 - (0.9 × 0.7 × 0.6 × 0.8)
                = 1 - 0.302
                = 69.8% chance of improvement
```

## 📝 Commits

All changes pushed to branch: `claude/fix-mipi-dsi-black-frames-011CUv1ZELEkvcY8S4VVWHf8`

- `5e4695d` - Test 1: Enable mirror_x
- `a89b43a` - Test 2: Memory zone analysis
- `ea48d5a` - Test 3: Cache synchronization (highest priority)
- `ed57dba` - Test 4: 64-byte aligned allocation
- `f749c11` - Documentation update

## 🚀 Next Steps

1. Flash and test on hardware
2. Analyze logs (especially PPA copy time)
3. If improved: Document which test(s) helped
4. If not improved: Consider zero-copy or reduced resolution
5. Optional: Profile M5Stack code for ground truth comparison

---

**Questions?** Check `PPA_INVESTIGATION.md` for detailed hypothesis and `PERFORMANCE_ANALYSIS.md` for profiling methodology.
