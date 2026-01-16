# LVGL Scale Widget - Quick Reference

## Basic Usage

```yaml
scale:
  id: my_scale
  mode: ROUND_OUTER              # ROUND_OUTER, ROUND_INNER, HORIZONTAL_TOP,
                                 # HORIZONTAL_BOTTOM, VERTICAL_LEFT, VERTICAL_RIGHT
  min_value: 0
  max_value: 100
  rotation: 135                  # Start angle (round modes)
  angle_range: 270               # Angle coverage (round modes)
```

## Modes Quick Reference

| Mode | Description | Best For |
|------|-------------|----------|
| `ROUND_OUTER` | Circular, ticks outside | Speedometers, gauges |
| `ROUND_INNER` | Circular, ticks inside | Compact gauges |
| `HORIZONTAL_TOP` | Horizontal, ticks on top | Progress bars |
| `HORIZONTAL_BOTTOM` | Horizontal, ticks on bottom | Sliders |
| `VERTICAL_LEFT` | Vertical, ticks on left | Thermometers |
| `VERTICAL_RIGHT` | Vertical, ticks on right | Volume meters |

## Ticks Quick Setup

```yaml
ticks:
  count: 21                      # Total ticks
  width: 2                       # Line thickness
  length: 10                     # Line length
  color: 0x808080               # Gray
  major:
    stride: 5                    # Every 5th tick
    width: 4
    length: 20
    label_show: true             # Show numbers
```

## Colored Sections (Zones)

```yaml
sections:
  - id: green_zone
    range_from: 0
    range_to: 60
    color: 0x00FF00              # Green
    width: 6

  - id: yellow_zone
    range_from: 60
    range_to: 80
    color: 0xFFFF00              # Yellow
    width: 6

  - id: red_zone
    range_from: 80
    range_to: 100
    color: 0xFF0000              # Red
    width: 6
```

## Common Patterns

### Speedometer (0-200 km/h)
```yaml
scale:
  id: speed
  width: 200
  height: 200
  mode: ROUND_OUTER
  min_value: 0
  max_value: 200
  rotation: 135
  angle_range: 270
  ticks:
    count: 21                    # Every 10 km/h
    major:
      stride: 5                  # Labels at 0, 50, 100, 150, 200
```

### Temperature (-20 to 50°C)
```yaml
scale:
  id: temp
  width: 50
  height: 200
  mode: VERTICAL_LEFT
  min_value: -20
  max_value: 50
  ticks:
    count: 15
    major:
      stride: 3                  # Labels every ~15°
```

### Progress Bar (0-100%)
```yaml
scale:
  id: progress
  width: 300
  height: 50
  mode: HORIZONTAL_TOP
  min_value: 0
  max_value: 100
  ticks:
    count: 11                    # Every 10%
    major:
      stride: 5                  # Labels at 0, 50, 100
```

## Automation Actions

### Update Scale
```yaml
lvgl.scale.update:
  id: my_scale
  min_value: 0
  max_value: 300
```

### Update Section
```yaml
lvgl.scale.section.update:
  id: red_zone
  range_from: 80
  range_to: 100
  color: 0xFF0000
```

## Parts for Styling

```yaml
scale:
  id: my_scale
  main:                          # Background
    bg_color: 0x000000
    arc_color: 0xCCCCCC          # Scale line
  indicator:                     # Major ticks
    line_color: 0xFFFFFF
    text_color: 0xFFFFFF         # Labels
  items:                         # Minor ticks
    line_color: 0x808080
```

## Common Colors

```yaml
# Primary Colors
0xFF0000    # Red
0x00FF00    # Green
0x0000FF    # Blue
0xFFFF00    # Yellow
0xFF00FF    # Magenta
0x00FFFF    # Cyan

# Grayscale
0xFFFFFF    # White
0xCCCCCC    # Light gray
0x808080    # Medium gray
0x333333    # Dark gray
0x000000    # Black

# Zones
0x00FF00    # Safe (Green)
0xFFFF00    # Warning (Yellow)
0xFF0000    # Danger (Red)
```

## Typical Rotation Values

```yaml
rotation: 0      # Top (12 o'clock)
rotation: 45     # Top-right
rotation: 90     # Right (3 o'clock)
rotation: 135    # Bottom-right (common for gauges)
rotation: 180    # Bottom (6 o'clock)
rotation: 225    # Bottom-left
rotation: 270    # Left (9 o'clock)
```

## Typical Angle Ranges

```yaml
angle_range: 360   # Full circle
angle_range: 270   # 3/4 circle (common for gauges)
angle_range: 180   # Half circle
angle_range: 120   # 1/3 circle
```

## File Locations

- **Implementation**: `components/lvgl/widgets/scale.py`
- **Examples**: `components/lvgl/widgets/scale_example.yaml`
- **Full Docs**: `components/lvgl/widgets/SCALE_WIDGET_README.md`
- **Summary**: `SCALE_WIDGET_IMPLEMENTATION.md`

## Need Help?

- Check `SCALE_WIDGET_README.md` for detailed documentation
- See `scale_example.yaml` for complete examples
- Reference other widgets (arc.py, slider.py) for similar patterns
