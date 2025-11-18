# OV5647 Image Quality Optimization Guide

## Overview

This guide documents the critical parameters affecting image quality (sharpness and noise) for the OV5647 camera sensor configurations in this project.

## Problem Analysis

### Initial Issue
Images from VGA (640x480) and 800x600 resolutions showed excessive noise and lacked sharpness compared to the proven 800x640 configuration from testov5647.

### Root Cause
The difference in image quality was traced to specific register configurations that controlled auto-exposure, auto-gain, and white balance settings.

## Critical Parameters for Image Quality

### 1. Auto Exposure/Gain Control (Register 0x3503)

**Key Finding:** The presence of register `0x3503` significantly affects image quality.

| Configuration | Register 0x3503 | Image Quality |
|--------------|-----------------|---------------|
| 800x640 (testov5647) | ❌ **ABSENT** | ✅ Sharp, low noise |
| 800x600 (before fix) | ✅ Present (0x00) | ❌ Noisy, less sharp |
| VGA (before fix) | ✅ Present (0x00) | ❌ Noisy, less sharp |

**Register 0x3503 values:**
- `0x00` = Auto exposure and auto gain enabled (forced mode)
- `0x03` = Manual exposure and manual gain
- **ABSENT** = Use sensor default (optimal for quality)

**Solution:** Remove register 0x3503 from configuration to allow the sensor to use its optimized default auto-exposure algorithm.

```c
// ❌ BAD - Forces auto mode, introduces noise
{0x3503, 0x00},  // Enable auto exposure and auto gain

// ✅ GOOD - Let sensor use optimized defaults
// (simply omit this register)
```

### 2. MIPI Clock Period (Register 0x4837)

**Impact:** Affects timing synchronization between sensor and ISP.

**Before fix (static value):**
```c
{0x4837, 0x24},  // Fixed MIPI pclk period
```

**After fix (calculated dynamically):**
```c
{0x4837, (1000000000 / (OV5647_IDI_CLOCK_RATE_640x480_30FPS / 4))},  // Calculated based on clock rate
```

**Why it matters:** Dynamic calculation ensures correct timing for different clock rates, preventing artifacts and improving consistency.

### 3. White Balance Settings (Registers 0x5180-0x519e)

**Finding:** The 800x640 configuration (good quality) does NOT include explicit AWB register settings, while other configs did.

**Registers removed:**
- 0x5180-0x519e: AWB gain, thresholds, and color correction matrices

**Reason:** Letting the ISP handle white balance automatically (via register 0x5000 bit 1) produces better results than forcing specific AWB parameters.

## Configuration Comparison

### 800x640 (Reference - Good Quality)
```c
// Clock: 100 MHz, 50 fps
{0x3035, 0x41},
{0x3036, ((OV5647_IDI_CLOCK_RATE_800x640_50FPS * 8 * 4) / 25000000)},

// ISP: All blocks enabled
{0x5000, 0xff},

// AEC/AGC: No 0x3503 register (uses sensor defaults)
{0x3f05, 0x02},
{0x3f06, 0x10},
// ... other AEC settings

// MIPI: Calculated timing
{0x4837, (1000000000 / (OV5647_IDI_CLOCK_RATE_800x640_50FPS / 4))},

// AWB: Not specified (ISP handles it)
```

### VGA & 800x600 (After Fix - Improved Quality)
```c
// Clock: 48 MHz, 30 fps
{0x3035, 0x21},
{0x3036, ((OV5647_IDI_CLOCK_RATE_640x480_30FPS * 8 * 4) / 25000000)},

// ISP: All blocks enabled
{0x5000, 0xff},

// AEC/AGC: No 0x3503 register (fixed - now matches 800x640)
{0x3f05, 0x02},
{0x3f06, 0x10},
// ... other AEC settings

// MIPI: Calculated timing (fixed)
{0x4837, (1000000000 / (OV5647_IDI_CLOCK_RATE_640x480_30FPS / 4))},

// AWB: Not specified (removed - now matches 800x640)
```

## ISP Settings Reference

### Register 0x5000 - ISP Control
This register enables various ISP blocks:
```c
{0x5000, 0xff},  // Enable all ISP blocks
```

**Bits:**
- Bit 7: LENC (Lens correction)
- Bit 6: Reserved
- Bit 5: Reserved
- Bit 4: Reserved
- Bit 3: BPC (Black/White pixel correction)
- Bit 2: WPC (White pixel correction)
- Bit 1: AWB (Auto white balance)
- Bit 0: AWB gain

**All enabled (0xff)** ensures maximum image quality processing.

### Lens Shading Correction (LSC)
```c
{0x583e, 0xf0},  // LSC max gain
{0x583f, 0x20},  // LSC min gain
```

These settings correct vignetting and lens imperfections.

## Recommended ISP Adjustments

Based on testov5647, these software ISP adjustments can further enhance quality:

### Brightness, Contrast, Saturation
```c
// From testov5647 comments (applied via ESP-IDF ISP API, not sensor registers)
brightness: 60    // Range typically -128 to 127
contrast: 145     // Range typically 0 to 255 (128 = neutral)
saturation: 135   // Range typically 0 to 255 (128 = neutral)
```

**Note:** These are applied at the ESP32-P4 ISP level using:
```c
esp_isp_color_configure(isp_handle, &color_cfg);
```

Not via OV5647 sensor registers.

## Summary of Changes

### Files Modified
- `components/mipi_dsi_cam/ov5647_custom_formats.h`

### Changes Applied

#### VGA (640x480) Configuration
1. ❌ Removed: `{0x3503, 0x00}` - Auto exposure/gain control
2. ✅ Changed: `{0x4837, 0x24}` → Calculated value based on clock rate
3. ❌ Removed: All AWB registers (0x5180-0x519e)

#### 800x600 Configuration
1. ❌ Removed: `{0x3503, 0x00}` - Auto exposure/gain control
2. ✅ Changed: `{0x4837, 0x24}` → Calculated value based on clock rate
3. ❌ Removed: All AWB registers (0x5180-0x519e)

### Expected Results
- ✅ Reduced image noise
- ✅ Improved sharpness
- ✅ Better auto-exposure performance
- ✅ Consistent quality across all resolutions (VGA, 800x600, 800x640)

## Testing Recommendations

1. **Compare resolutions:** Test VGA, 800x600, and 800x640 under same lighting
2. **Check exposure:** Verify auto-exposure adapts correctly to lighting changes
3. **Evaluate noise:** Check low-light performance for noise levels
4. **Verify colors:** Ensure white balance is accurate across different scenes
5. **Test sharpness:** Check fine detail reproduction in well-lit conditions

## Advanced Tuning

If further quality improvements are needed, consider adjusting these ESP-IDF ISP parameters:

### Color Adjustment
```yaml
esp_isp:
  brightness: 60      # Adjust exposure compensation
  contrast: 145       # Enhance detail separation
  saturation: 135     # Color vibrancy
```

### Noise Reduction
```yaml
esp_isp:
  denoise: true       # Enable noise reduction
  sharpen: medium     # Edge enhancement
```

### Exposure Control
```c
// Manual exposure override (if needed)
{0x3503, 0x03},  // Manual mode
{0x3500, 0x00},  // Exposure high
{0x3501, 0x30},  // Exposure mid
{0x3502, 0x00},  // Exposure low
```

**Note:** Only use manual exposure for specific use cases. Auto mode (sensor default) is preferred for general use.

## Conclusion

The key to optimal image quality on OV5647 is:

1. **Let the sensor handle auto-exposure** (no 0x3503 register)
2. **Calculate MIPI timing dynamically** (0x4837)
3. **Let the ISP handle white balance** (no explicit AWB registers)
4. **Enable all ISP processing blocks** (0x5000 = 0xff)

These principles are proven by the testov5647 800x640 configuration and now applied consistently across all resolutions.

## References

- **testov5647 repository:** Reference implementation with proven image quality
- **OV5647 datasheet:** Register definitions and recommended settings
- **ESP-IDF ISP documentation:** ESP32-P4 image signal processor API
- **Component location:** `components/mipi_dsi_cam/ov5647_custom_formats.h`

---

**Last Updated:** 2025-01-18
**Author:** youkorr
**Project:** test2_esp_video_esphome
