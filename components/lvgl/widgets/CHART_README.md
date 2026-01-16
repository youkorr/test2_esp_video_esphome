# LVGL Chart Widget Implementation for ESPHome

## Overview

This implementation provides full support for the LVGL v9.4 Chart widget (`lv_chart`) in ESPHome. The chart widget displays data visualizations including line charts, bar charts, and scatter plots with support for multiple data series.

## File Location

`/home/user/test2_esp_video_esphome/components/lvgl/widgets/chart.py`

## Implementation Details

### LVGL v9.4 API Support

The implementation uses the following LVGL v9.4 API functions:

1. **`lv_chart_create(parent)`** - Creates the chart widget
2. **`lv_chart_set_type(obj, type)`** - Sets chart type (LINE, BAR, SCATTER)
3. **`lv_chart_set_point_count(obj, count)`** - Sets number of data points
4. **`lv_chart_set_update_mode(obj, mode)`** - Sets how data updates (SHIFT, CIRCULAR)
5. **`lv_chart_set_range(obj, axis, min, max)`** - Sets axis range
6. **`lv_chart_set_div_line_count(obj, hdiv, vdiv)`** - Sets division line count
7. **`lv_chart_add_series(obj, color, axis)`** - Adds a data series

### Chart Types

- **LINE** - Connected line charts for continuous data
- **BAR** - Vertical or horizontal bar charts
- **SCATTER** - Point-based scatter plots
- **NONE** - Hidden series (useful for mixing types)

### Update Modes

- **SHIFT** - New data shifts old data left (default)
- **CIRCULAR** - Data wraps around in circular buffer

### Axes Support

- **Primary Y Axis** - Main vertical axis
- **Secondary Y Axis** - Additional vertical axis
- **Primary X Axis** - Main horizontal axis
- **Secondary X Axis** - Additional horizontal axis

Each axis can be configured with:
- Min/max range
- Division line count
- Custom labels

## Configuration Schema

### Chart Parameters

- **`type`** (enum, default: LINE): Chart type
  - LINE, BAR, SCATTER, NONE
- **`point_count`** (positive_int, default: 10): Number of data points
- **`update_mode`** (enum, default: SHIFT): How data updates
  - SHIFT, CIRCULAR
- **`series`** (list): List of data series configurations
- **`axis_primary_y`** (dict): Primary Y axis configuration
- **`axis_secondary_y`** (dict): Secondary Y axis configuration
- **`axis_primary_x`** (dict): Primary X axis configuration
- **`axis_secondary_x`** (dict): Secondary X axis configuration

### Series Parameters

- **`id`** (required): Series identifier
- **`color`** (color): Series color
- **`type`** (enum): Override chart type for this series
- **`points`** (list): Initial data points

### Axis Parameters

- **`min_value`** (int): Minimum axis value
- **`max_value`** (int): Maximum axis value
- **`div_line_count`** (positive_int): Number of division lines

## Usage Examples

### Basic Line Chart

```yaml
lvgl:
  - chart:
      id: temperature_chart
      x: 10
      y: 10
      width: 300
      height: 200
      type: LINE
      point_count: 20
      axis_primary_y:
        min_value: 0
        max_value: 100
        div_line_count: 5
      series:
        - id: temp_series
          color: 0xFF0000
```

### Multi-Series Chart

```yaml
lvgl:
  - chart:
      id: multi_chart
      type: LINE
      point_count: 50
      update_mode: CIRCULAR
      axis_primary_y:
        min_value: -50
        max_value: 50
        div_line_count: 10
      series:
        - id: series1
          color: 0xFF0000  # Red
        - id: series2
          color: 0x00FF00  # Green
        - id: series3
          color: 0x0000FF  # Blue
```

### Bar Chart

```yaml
lvgl:
  - chart:
      id: bar_chart
      type: BAR
      point_count: 12
      axis_primary_y:
        min_value: 0
        max_value: 1000
        div_line_count: 5
      series:
        - id: monthly_data
          color: 0x00AA00
          points: [100, 150, 200, 175, 225, 300, 350, 325, 275, 250, 200, 150]
```

### Scatter Plot

```yaml
lvgl:
  - chart:
      id: scatter_chart
      type: SCATTER
      point_count: 30
      axis_primary_y:
        min_value: 0
        max_value: 100
      axis_primary_x:
        min_value: 0
        max_value: 100
      series:
        - id: scatter_series
          color: 0x0080FF
```

### Mixed Chart Types

```yaml
lvgl:
  - chart:
      id: mixed_chart
      type: LINE
      point_count: 20
      series:
        - id: line_series
          type: LINE
          color: 0xFF0000
        - id: bar_series
          type: BAR
          color: 0x00FF00
```

## Dynamic Updates

Update chart data using actions:

```yaml
# Update a single point
on_....:
  - lvgl.chart.set_point:
      id: temperature_chart
      series_id: temp_series
      point_index: 0
      value: !lambda 'return id(temperature_sensor).state;'

# Add new data point (shifts/wraps based on update_mode)
on_....:
  - lvgl.chart.add_point:
      id: temperature_chart
      series_id: temp_series
      value: !lambda 'return id(temperature_sensor).state;'

# Clear all series data
on_....:
  - lvgl.chart.clear:
      id: temperature_chart
```

## Integration with Sensors

```yaml
sensor:
  - platform: bme280
    temperature:
      name: "Temperature"
      id: temp_sensor
      on_value:
        then:
          - lvgl.chart.add_point:
              id: temperature_chart
              series_id: temp_series
              value: !lambda 'return x;'

  - platform: bme280
    humidity:
      name: "Humidity"
      id: humidity_sensor
      on_value:
        then:
          - lvgl.chart.add_point:
              id: multi_chart
              series_id: humidity_series
              value: !lambda 'return x;'
```

## Features

### ✅ Implemented

- ✅ Create chart widget
- ✅ Set chart type (LINE, BAR, SCATTER)
- ✅ Configure point count
- ✅ Set update mode (SHIFT, CIRCULAR)
- ✅ Multiple data series support
- ✅ Axis configuration (4 axes)
- ✅ Range configuration per axis
- ✅ Division lines
- ✅ Series color customization
- ✅ Initial data points

### 🎯 Key Benefits

1. **Data Visualization** - Display time-series and statistical data
2. **Real-time Updates** - Live sensor data visualization
3. **Multiple Series** - Compare multiple data sets
4. **Flexible Types** - Line, bar, scatter, or mixed
5. **Configurable Axes** - Custom ranges and scales

## Widget Parts

The chart widget supports two parts for styling:

1. **MAIN** - Chart background and border
2. **ITEMS** - Data series and points

## Code Generation

The implementation generates C++ code that calls LVGL functions:

```cpp
// Example generated code
lv_chart_set_type(obj, LV_CHART_TYPE_LINE);
lv_chart_set_point_count(obj, 20);
lv_chart_set_update_mode(obj, LV_CHART_UPDATE_MODE_SHIFT);
lv_chart_set_range(obj, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
lv_chart_set_div_line_count(obj, 5, 5);
lv_chart_add_series(obj, lv_color_hex(0xFF0000), LV_CHART_AXIS_PRIMARY_Y);
```

## Dependencies

The chart widget uses the label widget for axis labels:

```python
def get_uses(self):
    return ("label",)
```

## LVGL Component Requirement

The implementation requires the LVGL CHART component to be enabled:

```python
lvgl_components_required.add("chart")
```

## Technical Notes

### Pattern Compliance

The implementation follows established patterns from:
- **canvas.py** - Drawing and layer-based rendering
- **lv_bar.py** - Range and value handling
- **arc.py** - Multi-part styling

### Type Safety

- Uses `LvType` for C++ type definitions
- Validates configuration with `cv.Schema`
- Processes values with `lv_int.process()` and `lv_color.process()`

## Use Cases

1. **Temperature Monitoring** - Display temperature trends over time
2. **Energy Monitoring** - Show power consumption graphs
3. **Data Logging** - Visualize historical sensor data
4. **Statistics** - Display statistical data in bar charts
5. **Comparisons** - Multi-series comparison charts

## References

- LVGL v9.4 Chart Documentation: https://docs.lvgl.io/9.4/widgets/chart.html
- ESPHome LVGL Integration: https://esphome.io/components/lvgl/
