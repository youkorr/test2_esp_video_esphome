# Fix: LVGL Font Compilation Error with LVGL 9.4.0

## Problem

When compiling your ESPHome configuration with LVGL 9.4.0, you get errors like:

```
src/esphome/components/font/font.cpp:33:8: error: 'struct lv_font_glyph_dsc_t' has no member named 'bpp'
   33 |   dsc->bpp = fe->get_bpp();
      |        ^~~

src/esphome/components/font/font.cpp:167:37: error: invalid conversion from 'const uint8_t* (*)(const lv_font_t*, uint32_t)' to 'const void* (*)(lv_font_glyph_dsc_t*, lv_draw_buf_t*)'
  167 |   this->lv_font_.get_glyph_bitmap = get_glyph_bitmap;
```

## Root Cause

This error occurs because ESPHome is using its **built-in font component**, which is NOT compatible with LVGL 9.x. The LVGL 9.x API changed significantly from LVGL 8.x:

- `lv_font_glyph_dsc_t` no longer has a `bpp` field → use `format` instead
- `get_glyph_bitmap` callback signature changed completely

This repository contains a **patched font component** with LVGL 9.x compatibility, but you must explicitly tell ESPHome to use it.

## Solution

Add `font` to your `external_components` configuration:

### Before (❌ WRONG)

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/youkorr/test2_esp_video_esphome
    components:
      - lvgl      # Only LVGL component
      - storage
      # Missing font!
```

### After (✅ CORRECT)

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/youkorr/test2_esp_video_esphome
    components:
      - lvgl      # LVGL v9.4
      - font      # ← ADD THIS LINE - Font with LVGL 9.x compatibility
      - storage
```

## Quick Fix

1. Open your ESPHome YAML configuration file
2. Find the `external_components:` section
3. Add `- font` to the `components:` list (right after `- lvgl`)
4. Save and recompile

## Why This Happens

When you use `external_components` with a specific list of components, ESPHome will:
- Use the custom component for items in the list (e.g., `lvgl`)
- Use the **built-in component** for items NOT in the list (e.g., `font`)

Since LVGL uses fonts for text rendering, and the built-in ESPHome font component doesn't support LVGL 9.x, you get compilation errors.

## For Local Development

If you're developing locally with `type: local`, the same rule applies:

```yaml
external_components:
  - source:
      type: local
      path: components
    components:
      - lvgl
      - font    # ← Still required!
```

## Technical Details

The patched font component includes:

1. **Updated callback signatures** for LVGL 9.x API
2. **`format` field** instead of deprecated `bpp`
3. **`stride` calculation** for bitmap rendering
4. **New glyph descriptor fields** (`gid.index`, `resolved_font`)

See `components/font/README.md` for full technical details.

## References

- [LVGL 9.x Migration Guide](https://docs.lvgl.io/9.0/CHANGELOG.html)
- [LVGL Font API Documentation](https://docs.lvgl.io/9.0/API/font/lv_font.html)
- [Font Component Source](components/font/)

## Still Having Issues?

1. Make sure you're using the latest version of this repository
2. Check that `font` appears in your `external_components` list
3. Clean your build directory: `esphome clean yourconfig.yaml`
4. Try compiling again

If the error persists, please open an issue with your full configuration.
