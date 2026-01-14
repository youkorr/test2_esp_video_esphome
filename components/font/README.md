# Font Component - LVGL 9.x Compatibility

This is a local override of the ESPHome font component with LVGL 9.x compatibility fixes.

## ⚠️ IMPORTANT: Required Configuration

**You MUST explicitly include this font component in your `external_components` configuration**, otherwise ESPHome will use its built-in font component which does NOT have LVGL 9.x compatibility.

### Correct Configuration

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/youkorr/test2_esp_video_esphome
    components:
      - lvgl    # LVGL v9.4
      - font    # ← REQUIRED: Font with LVGL 9.x compatibility
      # ... other components
```

### ❌ Common Mistake

If you forget to include `font` in the components list, you will get compilation errors like:
```
error: 'struct lv_font_glyph_dsc_t' has no member named 'bpp'
error: invalid conversion from 'const uint8_t* (*)(const lv_font_t*, uint32_t)' to 'const void* (*)(lv_font_glyph_dsc_t*, lv_draw_buf_t*)'
```

This happens because ESPHome is using its built-in (incompatible) font component instead of this one.

## Changes Made

### 1. Updated `get_glyph_bitmap` Callback Signature (font.h)

**LVGL 8.x:**
```cpp
static const uint8_t *get_glyph_bitmap(const lv_font_t *font, uint32_t unicode_letter);
```

**LVGL 9.x (updated):**
```cpp
static const void *get_glyph_bitmap(lv_font_glyph_dsc_t *g_dsc, lv_draw_buf_t *draw_buf);
```

### 2. Updated `get_glyph_bitmap` Implementation (font.cpp)

The implementation now extracts the font and unicode letter from the glyph descriptor structure:
- Accesses font via `g_dsc->resolved_font->dsc`
- Retrieves unicode letter from `g_dsc->gid.index`

### 3. Replaced `bpp` Field with `format` (font.cpp)

In LVGL 9.x, the `lv_font_glyph_dsc_t` structure no longer has a `bpp` field. Instead, it uses a `format` field of type `lv_font_glyph_format_t`.

**Mapping:**
- 1 bpp → `LV_FONT_GLYPH_FORMAT_A1`
- 2 bpp → `LV_FONT_GLYPH_FORMAT_A2`
- 4 bpp → `LV_FONT_GLYPH_FORMAT_A4`
- 8 bpp → `LV_FONT_GLYPH_FORMAT_A8`

### 4. Added Stride Calculation (font.cpp)

LVGL 9.x requires the `stride` field to be set, which represents bytes per row:
```cpp
dsc->stride = (gd->width * bpp + 7) / 8;
```

### 5. Set Additional Fields (font.cpp)

- `dsc->gid.index = unicode_letter;` - Stores unicode for bitmap retrieval
- `dsc->resolved_font = font;` - Allows bitmap callback to access the font

## Compatibility

- **LVGL Version:** 9.4.0
- **ESPHome Version:** Based on latest dev branch (as of January 2026)

## References

- [LVGL 9.x Font API Documentation](https://docs.lvgl.io/9.0/API/font/lv_font.html)
- [LVGL Adding a New Font Engine](https://docs.lvgl.io/latest/en/html/main-modules/fonts/new_font_engine.html)
- [ESPHome Font Component](https://github.com/esphome/esphome/tree/dev/esphome/components/font)
