# LVGL Arc Label Widget Implementation for ESPHome

## Overview

This implementation provides support for the LVGL v9.4 Arc Label widget (`lv_arclabel`) in ESPHome. The Arc Label widget displays text along a curved or circular path, perfect for circular displays and decorative text elements.

## File Location

`/home/user/test2_esp_video_esphome/components/lvgl/widgets/arclabel.py`

## Implementation Details

### LVGL v9.4 API Support

The implementation uses LVGL v9.4 Arc Label API functions for displaying text along curved paths.

### Features

- **Curved Text** - Display text along arc paths
- **Configurable Radius** - Adjustable arc curvature
- **Angle Control** - Set start and end angles
- **Rotation** - Rotate the entire arc
- **Circular Displays** - Perfect for round screens

## Configuration Schema

### Widget Parameters

- **`text`** (string, required): Text to display along the arc
- **`radius`** (int, default: 100): Arc radius in pixels
- **`start_angle`** (int, default: 0): Starting angle in degrees (0-360)
- **`end_angle`** (int, default: 360): Ending angle in degrees (0-360)
- **`rotation`** (int, default: 0): Rotation offset in degrees

All standard text properties (color, font) are supported.

## Usage Examples

### Basic Circular Text

```yaml
lvgl:
  - arclabel:
      id: clock_text
      text: "12 3 6 9"
      x: CENTER
      y: CENTER
      radius: 100
      start_angle: 0
      end_angle: 360
      text_color: 0x000000
      text_font: roboto_16
```

### Curved Label

```yaml
lvgl:
  - arclabel:
      id: curved_label
      text: "Curved Display"
      x: 120
      y: 120
      radius: 80
      start_angle: 45
      end_angle: 315
      rotation: 0
      text_color: 0x2196F3
      text_font: roboto_14_bold
```

### Clock Face

```yaml
lvgl:
  - arclabel:
      id: hour_markers
      text: "12  1  2  3  4  5  6  7  8  9  10  11"
      x: CENTER
      y: CENTER
      radius: 120
      start_angle: 0
      end_angle: 360
      text_color: 0x000000
```

## Use Cases

1. **Clock Faces** - Hour/minute markers
2. **Circular Menus** - Menu items around circle
3. **Decorative Text** - Artistic curved text
4. **Gauges** - Value labels on gauges
5. **Round Displays** - Optimized for circular screens

## References

- [LVGL v9.4 Arc Label Documentation](https://docs.lvgl.io/9.4/details/widgets/arclabel.html)

---

**Implementation Status:** ✅ Complete  
**LVGL Version:** 9.4.0  
**Best For:** Circular displays and decorative text
