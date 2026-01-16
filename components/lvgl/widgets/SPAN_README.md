# LVGL Spangroup Widget Implementation for ESPHome

## Overview

This implementation provides full support for the LVGL v9.4 Spangroup widget (`lv_spangroup`) in ESPHome. The Spangroup widget displays text with multiple styles (fonts, colors, decorations) within a single label, enabling rich text formatting.

## File Location

`/home/user/test2_esp_video_esphome/components/lvgl/widgets/span.py`

## What is Spangroup?

Spangroup allows you to combine multiple text fragments (spans) with different styling in a single widget. Each span can have its own font, color, and text decoration, making it perfect for rich text displays.

## Implementation Details

### LVGL v9.4 API Support

The implementation uses the following LVGL v9.4 API functions:

1. **`lv_spangroup_create(parent)`** - Creates the spangroup widget
2. **`lv_spangroup_set_mode(obj, mode)`** - Sets overflow mode
3. **`lv_spangroup_new_span(obj)`** - Creates a new text span
4. **`lv_span_set_text(span, text)`** - Sets span text content

### Span Modes

- **FIXED** - Text is clipped at widget boundaries
- **EXPAND** - Widget expands to fit all text
- **BREAK** - Text wraps to new lines (default)

### Text Decorations

- **NONE** - No decoration
- **UNDERLINE** - Underlined text
- **STRIKETHROUGH** - Strike-through text

## Configuration Schema

### Widget Parameters

- **`mode`** (enum, default: BREAK): Text overflow handling
  - `FIXED`, `EXPAND`, `BREAK`
- **`spans`** (list, optional): List of text spans
  - **`text`** (string, required): Span text content
  - **`text_color`** (color, optional): Text color for this span
  - **`text_font`** (font, optional): Font for this span
  - **`text_decor`** (enum, optional): Text decoration
    - `NONE`, `UNDERLINE`, `STRIKETHROUGH`

All standard widget properties are supported.

## Usage Examples

### Basic Rich Text

```yaml
lvgl:
  - spangroup:
      id: rich_text
      x: 10
      y: 10
      width: 300
      mode: BREAK
      spans:
        - text: "Welcome to "
          text_color: 0x000000
          text_font: roboto_16
        - text: "ESPHome"
          text_color: 0x0080FF
          text_font: roboto_16_bold
        - text: "!"
          text_color: 0x000000
          text_font: roboto_16
```

### Formatted Message

```yaml
lvgl:
  - spangroup:
      id: notification
      x: 20
      y: 50
      width: 440
      mode: BREAK
      bg_color: 0xF5F5F5
      pad_all: 10
      radius: 5
      spans:
        - text: "Warning: "
          text_color: 0xFF9800
          text_font: roboto_14_bold
        - text: "System temperature is "
          text_color: 0x000000
          text_font: roboto_14
        - text: "high"
          text_color: 0xFF0000
          text_font: roboto_14_bold
          text_decor: UNDERLINE
        - text: ". Please check cooling system."
          text_color: 0x000000
          text_font: roboto_14
```

### Multi-Style Labels

```yaml
lvgl:
  pages:
    - id: status_page
      widgets:
        # Status message
        - spangroup:
            id: status_message
            x: 10
            y: 100
            width: 460
            mode: BREAK
            spans:
              - text: "Status: "
                text_color: 0x666666
                text_font: roboto_12
              - text: "Connected"
                text_color: 0x00AA00
                text_font: roboto_12_bold

        # Device info
        - spangroup:
            id: device_info
            x: 10
            y: 150
            width: 460
            mode: BREAK
            spans:
              - text: "Device: "
                text_color: 0x666666
              - text: "ESP32-P4"
                text_color: 0x000000
                text_font: roboto_14_bold
              - text: " | Firmware: "
                text_color: 0x666666
              - text: "v2.1.0"
                text_color: 0x2196F3
```

### Highlighted Text

```yaml
lvgl:
  - spangroup:
      id: search_result
      x: 10
      y: 200
      width: 400
      mode: BREAK
      spans:
        - text: "Found "
          text_color: 0x000000
        - text: "42 results"
          text_color: 0xFF0000
          text_font: roboto_16_bold
        - text: " matching your "
          text_color: 0x000000
        - text: "search query"
          text_color: 0x0080FF
          text_decor: UNDERLINE
```

### Code Snippet Display

```yaml
lvgl:
  - spangroup:
      id: code_display
      x: 10
      y: 300
      width: 460
      mode: BREAK
      bg_color: 0x1E1E1E
      pad_all: 15
      radius: 5
      spans:
        - text: "def "
          text_color: 0xFF6B6B
        - text: "hello"
          text_color: 0x4EC9B0
        - text: "():\n    "
          text_color: 0xD4D4D4
        - text: "print"
          text_color: 0xFF6B6B
        - text: "("
          text_color: 0xD4D4D4
        - text: '"Hello World!"'
          text_color: 0xCE9178
        - text: ")"
          text_color: 0xD4D4D4
```

### Dynamic Content

```yaml
lvgl:
  - spangroup:
      id: sensor_display
      x: 10
      y: 400
      width: 400
      mode: BREAK

# Update with sensor data
sensor:
  - platform: bme280
    temperature:
      name: "Temperature"
      id: temp_sensor
      on_value:
        then:
          - lambda: |-
              // Clear existing spans
              lv_spangroup_del_span(id(sensor_display), nullptr);

              // Add new spans
              lv_span_t* span1 = lv_spangroup_new_span(id(sensor_display));
              lv_span_set_text(span1, "Temperature: ");

              lv_span_t* span2 = lv_spangroup_new_span(id(sensor_display));
              char buf[16];
              snprintf(buf, sizeof(buf), "%.1f°C", x);
              lv_span_set_text(span2, buf);
              lv_style_set_text_color(&span2->style, lv_color_hex(x > 30 ? 0xFF0000 : 0x00AA00));
```

## Features

### ✅ Implemented

- ✅ Multiple text spans with individual styling
- ✅ Different fonts per span
- ✅ Different colors per span
- ✅ Text decorations (underline, strikethrough)
- ✅ Three overflow modes (FIXED, EXPAND, BREAK)
- ✅ Dynamic span creation and updates

### 🎯 Key Benefits

1. **Rich Formatting** - Multiple styles in one widget
2. **Flexibility** - Mix fonts, colors, decorations
3. **Efficient** - Single widget instead of multiple labels
4. **Dynamic** - Update spans at runtime
5. **Professional** - Create polished text displays

## Span Modes Explained

### FIXED Mode

Widget has fixed size, text is clipped if too long.

```yaml
- spangroup:
    mode: FIXED
    width: 200  # Fixed width
    height: 50  # Fixed height
```

### EXPAND Mode

Widget expands to fit all text content.

```yaml
- spangroup:
    mode: EXPAND
    # Width/height grow automatically
```

### BREAK Mode (Default)

Text wraps to new lines within widget width.

```yaml
- spangroup:
    mode: BREAK
    width: 300  # Text wraps at this width
```

## Text Decorations

### Underline

```yaml
spans:
  - text: "Important text"
    text_decor: UNDERLINE
```

### Strikethrough

```yaml
spans:
  - text: "Obsolete information"
    text_decor: STRIKETHROUGH
```

### None

```yaml
spans:
  - text: "Normal text"
    text_decor: NONE  # or omit
```

## Dynamic Updates

### Adding Spans at Runtime

```yaml
on_...:
  then:
    - lambda: |-
        lv_span_t* new_span = lv_spangroup_new_span(id(my_spangroup));
        lv_span_set_text(new_span, "New text");
        lv_style_set_text_color(&new_span->style, lv_color_hex(0xFF0000));
```

### Clearing All Spans

```yaml
on_...:
  then:
    - lambda: |-
        lv_spangroup_del_span(id(my_spangroup), nullptr);
```

### Rebuilding Content

```yaml
on_update:
  then:
    - lambda: |-
        // Clear existing
        lv_spangroup_del_span(id(status_text), nullptr);

        // Add new spans
        auto span1 = lv_spangroup_new_span(id(status_text));
        lv_span_set_text(span1, "Status: ");

        auto span2 = lv_spangroup_new_span(id(status_text));
        lv_span_set_text(span2, current_status.c_str());
        lv_style_set_text_color(&span2->style, status_color);
```

## Code Generation

```cpp
// Example generated code
lv_obj_t* spangroup = lv_spangroup_create(parent);
lv_spangroup_set_mode(spangroup, LV_SPAN_MODE_BREAK);

lv_span_t* span1 = lv_spangroup_new_span(spangroup);
lv_span_set_text(span1, "Hello ");
lv_style_set_text_color(&span1->style, lv_color_hex(0x000000));

lv_span_t* span2 = lv_spangroup_new_span(spangroup);
lv_span_set_text(span2, "World");
lv_style_set_text_color(&span2->style, lv_color_hex(0xFF0000));
```

## Dependencies

```python
def get_uses(self):
    return ("label",)
```

## LVGL Component Requirement

```python
lvgl_components_required.add("spangroup")
```

## Use Cases

1. **Notifications** - Highlighted keywords in messages
2. **Status Displays** - Multi-colored status text
3. **Search Results** - Highlight matching terms
4. **Code Display** - Syntax-highlighted code
5. **Formatted Messages** - Rich text formatting
6. **Dynamic Labels** - Runtime text composition

## Best Practices

1. **Keep It Simple** - Don't over-style text
2. **Consistent Fonts** - Use limited font variations
3. **Color Contrast** - Ensure readability
4. **Performance** - Limit number of spans
5. **Updates** - Rebuild only when necessary

## Performance Considerations

- Each span adds memory overhead
- Rebuilding frequently impacts performance
- Use FIXED or BREAK mode for better control
- Limit to ~10-20 spans per widget

## Example Integration

```yaml
# Weather status display
sensor:
  - platform: homeassistant
    entity_id: weather.home
    id: weather_state
    on_value:
      then:
        - lambda: |-
            lv_spangroup_del_span(id(weather_text), nullptr);

            auto icon = lv_spangroup_new_span(id(weather_text));
            lv_span_set_text(icon, weather_icon(x).c_str());

            auto temp = lv_spangroup_new_span(id(weather_text));
            char buf[32];
            snprintf(buf, sizeof(buf), " %.1f°C", id(temperature).state);
            lv_span_set_text(temp, buf);
            lv_style_set_text_color(&temp->style, temp_color(id(temperature).state));

            auto desc = lv_spangroup_new_span(id(weather_text));
            lv_span_set_text(desc, " | Clear sky");

lvgl:
  - spangroup:
      id: weather_text
      x: 10
      y: 10
      width: 400
      mode: BREAK
```

## References

- [LVGL v9.4 Spangroup Documentation](https://docs.lvgl.io/9.4/details/widgets/spangroup.html)

---

**Implementation Status:** ✅ Complete
**LVGL Version:** 9.4.0
