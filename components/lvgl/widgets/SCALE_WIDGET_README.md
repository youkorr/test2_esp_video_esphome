# LVGL v9.4 Scale Widget for ESPHome

## Overview

The Scale widget is a versatile LVGL v9.4 component for displaying measurement scales in various orientations. It replaces the obsolete Meter widget from LVGL v8.x and provides more flexibility for creating gauges, thermometers, progress indicators, and other measurement displays.

## Features

- **Multiple scale modes**: horizontal, vertical, and circular (round inner/outer)
- **Configurable tick marks**: Both major and minor ticks with independent styling
- **Value range**: Configurable minimum and maximum values
- **Colored sections**: Define colored zones for different value ranges (e.g., green/yellow/red zones)
- **Label support**: Automatic labels for major ticks
- **Rotation**: Configurable start angle and angle range for circular scales
- **Parts**: MAIN (background), INDICATOR (major ticks), ITEMS (minor ticks)

## Configuration Schema

### Basic Configuration

```yaml
scale:
  id: my_scale
  mode: ROUND_OUTER          # Scale orientation
  min_value: 0               # Minimum value
  max_value: 100             # Maximum value
  rotation: 135              # Start angle (for round modes)
  angle_range: 270           # Angle coverage (for round modes)
```

### Scale Modes

The `mode` parameter determines the scale orientation:

- **`ROUND_OUTER`**: Circular scale with ticks on the outside (default)
- **`ROUND_INNER`**: Circular scale with ticks on the inside
- **`HORIZONTAL_TOP`**: Horizontal scale with ticks on top
- **`HORIZONTAL_BOTTOM`**: Horizontal scale with ticks on bottom
- **`VERTICAL_LEFT`**: Vertical scale with ticks on left
- **`VERTICAL_RIGHT`**: Vertical scale with ticks on right

### Tick Configuration

```yaml
scale:
  id: my_scale
  ticks:
    count: 21                  # Total number of ticks
    width: 2                   # Line width of minor ticks
    length: 10                 # Length of minor ticks
    color: 0x808080           # Color of minor ticks
    radial_offset: 0          # Offset from scale line
    major:                    # Major tick configuration
      stride: 5               # Every Nth tick is major
      width: 4                # Line width of major ticks
      length: 20              # Length of major ticks
      color: 0xFFFFFF        # Color of major ticks
      radial_offset: 0       # Offset from scale line
      label_show: true       # Show labels on major ticks
      label_gap: 4           # Distance between tick and label
```

### Sections (Colored Zones)

Sections allow you to define colored ranges on the scale:

```yaml
scale:
  id: my_scale
  sections:
    - id: safe_zone
      range_from: 0
      range_to: 60
      color: 0x00FF00         # Green
      width: 6

    - id: warning_zone
      range_from: 60
      range_to: 80
      color: 0xFFFF00         # Yellow
      width: 6

    - id: danger_zone
      range_from: 80
      range_to: 100
      color: 0xFF0000         # Red
      width: 6
```

## Parts and Styling

The scale widget has three main parts that can be styled independently:

### MAIN Part
The background and main scale line:

```yaml
scale:
  id: my_scale
  main:
    bg_color: 0x000000
    bg_opa: 0.5
    arc_width: 2              # Scale arc line width (for round modes)
    arc_color: 0xCCCCCC       # Scale arc line color
```

### INDICATOR Part
Major ticks and their labels:

```yaml
scale:
  id: my_scale
  indicator:
    line_color: 0xFFFFFF      # Major tick color
    line_width: 4             # Major tick width
    text_color: 0xFFFFFF      # Label text color
    text_font: montserrat_14  # Label font
```

### ITEMS Part
Minor ticks:

```yaml
scale:
  id: my_scale
  items:
    line_color: 0x808080      # Minor tick color
    line_width: 2             # Minor tick width
```

## Usage Examples

### Example 1: Speedometer (Circular Scale)

```yaml
scale:
  id: speedometer
  x: 50
  y: 50
  width: 200
  height: 200
  mode: ROUND_OUTER
  min_value: 0
  max_value: 200
  rotation: 135               # Start at 135 degrees
  angle_range: 270            # Cover 270 degrees
  ticks:
    count: 21                 # 21 ticks (every 10 km/h)
    width: 2
    length: 10
    color: 0x808080
    major:
      stride: 5               # Major tick every 50 km/h
      width: 4
      length: 20
      color: 0xFFFFFF
      label_show: true
  sections:
    - id: normal_speed
      range_from: 0
      range_to: 120
      color: 0x00FF00         # Green zone
      width: 6
    - id: high_speed
      range_from: 120
      range_to: 160
      color: 0xFFFF00         # Yellow zone
      width: 6
    - id: overspeed
      range_from: 160
      range_to: 200
      color: 0xFF0000         # Red zone
      width: 6
```

### Example 2: Temperature Scale (Vertical)

```yaml
scale:
  id: thermometer
  x: 50
  y: 50
  width: 50
  height: 200
  mode: VERTICAL_LEFT
  min_value: -20
  max_value: 50
  ticks:
    count: 15
    width: 2
    length: 8
    color: 0x808080
    major:
      stride: 3               # Major tick every ~15 degrees
      width: 3
      length: 12
      color: 0x000000
      label_show: true
  sections:
    - id: freezing
      range_from: -20
      range_to: 0
      color: 0x0080FF         # Blue
      width: 5
    - id: normal
      range_from: 0
      range_to: 25
      color: 0x00FF00         # Green
      width: 5
    - id: hot
      range_from: 25
      range_to: 50
      color: 0xFF0000         # Red
      width: 5
```

### Example 3: Horizontal Progress Scale

```yaml
scale:
  id: progress_scale
  x: 50
  y: 150
  width: 300
  height: 50
  mode: HORIZONTAL_TOP
  min_value: 0
  max_value: 100
  ticks:
    count: 11                 # Ticks at 0, 10, 20, ..., 100
    width: 2
    length: 8
    major:
      stride: 5               # Major ticks at 0, 50, 100
      width: 3
      length: 15
      label_show: true
```

### Example 4: Pressure Gauge (Round Inner)

```yaml
scale:
  id: pressure_gauge
  x: 50
  y: 50
  width: 180
  height: 180
  mode: ROUND_INNER
  min_value: 0
  max_value: 10
  rotation: 135
  angle_range: 270
  ticks:
    count: 21
    width: 2
    length: 10
    color: 0x808080
    major:
      stride: 5
      width: 4
      length: 20
      color: 0xFFFFFF
      label_show: true
  main:
    arc_width: 8
    arc_color: 0x333333
    arc_opa: 0.5
```

## Automation Actions

### Update Scale Configuration

```yaml
lvgl.scale.update:
  id: my_scale
  mode: ROUND_OUTER
  min_value: 0
  max_value: 300
  rotation: 90
  angle_range: 180
```

### Update Section

```yaml
lvgl.scale.section.update:
  id: danger_zone
  range_from: 150
  range_to: 200
  color: 0xFF0000
  width: 8
```

## Integration with Sensors

The scale widget provides the visual scale, but typically you'll want to add an indicator (needle, arc, or line) to show the current value. Here's an example pattern:

```yaml
sensor:
  - platform: template
    name: "Speed"
    id: speed_sensor
    on_value:
      then:
        # Update needle position based on sensor value
        # Note: You would need to create a separate indicator widget
        # (arc, line, or image) and update its position
        - lambda: |-
            // Calculate needle angle based on speed_sensor->state
            // and update needle position
```

## Migration from Meter Widget

If you're migrating from the LVGL v8.x meter widget:

| Meter (v8.x) | Scale (v9.4) |
|--------------|--------------|
| `lv_meter_create()` | `lv_scale_create()` |
| `lv_meter_set_scale_ticks()` | `ticks.count` configuration |
| `lv_meter_set_scale_major_ticks()` | `ticks.major.stride` configuration |
| `lv_meter_add_arc()` | `sections` configuration |
| `lv_meter_add_needle_line()` | Separate line widget with rotation |

## API Reference

### LVGL v9.4 Functions Used

The scale widget implementation uses these LVGL v9.4 API functions:

- `lv_scale_create(parent)` - Create scale widget
- `lv_scale_set_mode(scale, mode)` - Set scale orientation
- `lv_scale_set_range(scale, min, max)` - Set value range
- `lv_scale_set_rotation(scale, angle)` - Set rotation angle
- `lv_scale_set_angle_range(scale, range)` - Set angle coverage
- `lv_scale_set_total_tick_count(scale, count)` - Set tick count
- `lv_scale_set_major_tick_every(scale, stride)` - Set major tick frequency
- `lv_scale_set_label_show(scale, show)` - Enable/disable labels
- `lv_scale_add_section(scale)` - Add colored section
- `lv_scale_section_set_range(section, start, end)` - Set section range

## Tips and Best Practices

1. **Tick Count**: Choose tick counts that divide evenly into your value range for clean labeling
2. **Major Tick Stride**: Typical values are 3, 5, or 10 depending on total tick count
3. **Angle Range**: For circular scales, 270° provides good visibility (avoids bottom area)
4. **Rotation**: 135° rotation (starting at bottom-left) is common for speedometer-style gauges
5. **Section Width**: Make section lines wider than tick lines for better visibility
6. **Label Gap**: Adjust based on your font size to prevent label overlap with ticks
7. **Color Contrast**: Ensure good contrast between ticks, labels, and background

## Troubleshooting

### Labels Not Showing
- Ensure `label_show: true` in major tick configuration
- Check that font is properly loaded
- Verify label_gap is appropriate for your scale size

### Sections Not Visible
- Ensure section width is greater than tick width
- Check color opacity (use fully opaque colors like 0xFF0000 not 0x00FF0000)
- Verify section ranges are within min/max values

### Ticks Overlapping
- Reduce tick count
- Increase scale size
- Adjust tick length

### Circular Scale Cut Off
- Ensure width and height are equal for round modes
- Increase widget size
- Adjust padding and margins

## References

- [LVGL v9.4 Scale Documentation](https://docs.lvgl.io/master/widgets/scale.html)
- [ESPHome LVGL Component](https://esphome.io/components/lvgl/index.html)
- [LVGL v9.4 Migration Guide](https://docs.lvgl.io/master/intro/migrate.html)
