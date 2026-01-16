# LVGL v9.4 Calendar Widget for ESPHome

## Overview

This implementation provides a complete LVGL v9.4 Calendar widget for ESPHome, allowing users to display interactive calendars with date selection, navigation, and highlighting capabilities.

## Features

- **Calendar Display**: Shows month/year with day grid
- **Date Navigation**: Navigate between months and years
- **Today Marker**: Visual indicator for today's date
- **Date Selection**: Select dates with callback support
- **Highlighted Dates**: Highlight specific dates (holidays, events, etc.)
- **Customizable Styling**: Full control over appearance via parts (MAIN, ITEMS)
- **Dynamic Updates**: Runtime updates via automation actions

## Files Modified/Created

### 1. `/home/user/test2_esp_video_esphome/components/lvgl/widgets/calendar.py`
Main Python implementation with:
- `CalendarType` class extending `WidgetType`
- `lv_calendar_t` type definition with year, month, day values
- Date schema validation (year: 1970-2099, month: 1-12, day: 1-31)
- `calendar_spec` widget specification
- `lvgl.calendar.update` automation action

### 2. `/home/user/test2_esp_video_esphome/components/lvgl/lvgl_esphome.h`
Added C++ class definition:
- `LvCalendarType` class extending `LvCompound`
- Methods: `get_selected_year()`, `get_selected_month()`, `get_selected_day()`
- Uses LVGL v9.4 API: `lv_calendar_get_pressed_date()`

## LVGL v9.4 API Usage

The implementation uses these LVGL v9.4 calendar functions:

```c
lv_obj_t * lv_calendar_create(lv_obj_t * parent);
void lv_calendar_set_today_date(lv_obj_t * obj, uint16_t year, uint8_t month, uint8_t day);
void lv_calendar_set_showed_date(lv_obj_t * obj, uint16_t year, uint8_t month, uint8_t day);
void lv_calendar_set_highlighted_dates(lv_obj_t * obj, lv_calendar_date_t dates[], uint16_t num);
lv_calendar_date_t * lv_calendar_get_pressed_date(lv_obj_t * obj, lv_calendar_date_t * date);
```

## Configuration Schema

### Basic Configuration

```yaml
calendar:
  id: my_calendar
  x: 10
  y: 10
  width: 300
  height: 300
  today_date:
    year: 2024
    month: 12
    day: 15
  showed_date:
    year: 2024
    month: 12
    day: 1
  highlighted_dates:
    - year: 2024
      month: 12
      day: 25
```

### Configuration Options

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `today_date` | Date | No | - | Sets today's date marker |
| `showed_date` | Date | No | - | Initial month/year to display |
| `highlighted_dates` | List[Date] | No | [] | Dates to highlight |

### Date Schema

```yaml
date:
  year: 2024   # Range: 1970-2099
  month: 12    # Range: 1-12
  day: 15      # Range: 1-31
```

## Parts and Styling

The calendar widget supports two main parts:

### MAIN Part
The overall calendar container including header (month/year display).

```yaml
calendar:
  main:
    bg_color: 0xFFFFFF
    border_width: 2
    border_color: 0x0000FF
    radius: 10
    pad_all: 5
```

### ITEMS Part
Individual day cells in the calendar grid.

```yaml
calendar:
  items:
    bg_color: 0xF0F0F0
    text_color: 0x000000
    # Today's date
    checked:
      bg_color: 0xFF0000
      text_color: 0xFFFFFF
    # Selected date
    pressed:
      bg_color: 0x0000FF
      text_color: 0xFFFFFF
    # Highlighted dates
    focused:
      bg_color: 0xFFFF00
      text_color: 0x000000
```

## Events

### on_value
Triggered when a date is selected. Provides year, month, day values.

```yaml
calendar:
  id: my_calendar
  on_value:
    - logger.log:
        format: "Selected: %d-%02d-%02d"
        args: ['x.year', 'x.month', 'x.day']
    - lambda: |-
        ESP_LOGI("calendar", "Date: %d-%02d-%02d",
                 x.year, x.month, x.day);
```

## Automation Actions

### lvgl.calendar.update

Update calendar properties at runtime.

```yaml
button:
  - platform: gpio
    pin: GPIO0
    on_press:
      - lvgl.calendar.update:
          id: my_calendar
          today_date:
            year: 2025
            month: 1
            day: 1
          showed_date:
            year: 2025
            month: 1
            day: 1
          highlighted_dates:
            - year: 2025
              month: 1
              day: 1
            - year: 2025
              month: 1
              day: 15
```

## Integration with ESPHome Time Component

```yaml
time:
  - platform: sntp
    id: sntp_time
    on_time:
      - seconds: 0
        minutes: 0
        hours: 0
        then:
          - lvgl.calendar.update:
              id: my_calendar
              today_date:
                year: !lambda 'return id(sntp_time).now().year;'
                month: !lambda 'return id(sntp_time).now().month;'
                day: !lambda 'return id(sntp_time).now().day_of_month;'
```

## Complete Example

```yaml
lvgl:
  displays:
    - my_display
  pages:
    - id: main_page
      widgets:
        - calendar:
            id: holiday_calendar
            width: 320
            height: 300
            today_date:
              year: 2024
              month: 12
              day: 15
            showed_date:
              year: 2024
              month: 12
              day: 1
            highlighted_dates:
              # Christmas
              - year: 2024
                month: 12
                day: 25
              # New Year's Eve
              - year: 2024
                month: 12
                day: 31
            main:
              bg_color: 0xFFFFFF
              border_width: 2
              radius: 5
            items:
              bg_color: 0xF5F5F5
              text_color: 0x000000
              radius: 3
              checked:  # Today
                bg_color: 0xFF4444
                text_color: 0xFFFFFF
              pressed:  # Selected
                bg_color: 0x4444FF
                text_color: 0xFFFFFF
              focused:  # Highlighted
                bg_color: 0xFFDD44
                text_color: 0x000000
            on_value:
              - logger.log:
                  format: "Date selected: %d-%02d-%02d"
                  args: ['x.year', 'x.month', 'x.day']
```

## Implementation Details

### Python Type System

The calendar uses `LvType` with three return values:

```python
lv_calendar_t = LvType(
    "LvCalendarType",
    parents=(LvCompound,),
    largs=[
        (cg.uint16, "year"),
        (cg.uint8, "month"),
        (cg.uint8, "day"),
    ],
    lvalue=lambda w: [
        w.var.get_selected_year(),
        w.var.get_selected_month(),
        w.var.get_selected_day(),
    ],
    has_on_value=True,
)
```

### C++ Type System

```cpp
#ifdef USE_LVGL_CALENDAR
class LvCalendarType : public LvCompound {
 public:
  uint16_t get_selected_year() {
    lv_calendar_date_t date;
    lv_calendar_get_pressed_date(this->obj, &date);
    return date.year;
  }
  uint8_t get_selected_month() {
    lv_calendar_date_t date;
    lv_calendar_get_pressed_date(this->obj, &date);
    return date.month;
  }
  uint8_t get_selected_day() {
    lv_calendar_date_t date;
    lv_calendar_get_pressed_date(this->obj, &date);
    return date.day;
  }
};
#endif  // USE_LVGL_CALENDAR
```

### Highlighted Dates Array Generation

The implementation creates static arrays for highlighted dates:

```python
dates_array_str = "{" + ", ".join(dates_elements) + "}"
dates_var = cg.RawExpression(
    f"static lv_calendar_date_t {dates_array_id}[] = {dates_array_str}"
)
lv_add(dates_var)
lv.calendar_set_highlighted_dates(w.obj, dates_array_id, dates_count)
```

This generates C++ code like:
```cpp
static lv_calendar_date_t obj_highlighted_dates[] = {{2024, 12, 25}, {2024, 12, 31}};
lv_calendar_set_highlighted_dates(obj, obj_highlighted_dates, 2);
```

## Pattern Compliance

This implementation follows ESPHome LVGL patterns:

1. **Based on buttonmatrix.py**: Grid-based day layout
2. **Based on dropdown.py**: Selection handling
3. **Widget Registration**: Automatic via `WidgetType.__init__`
4. **Component Requirements**: Uses `lvgl_components_required.add("CALENDAR")`
5. **Parts System**: Supports MAIN and ITEMS parts
6. **Automation**: Provides `lvgl.calendar.update` action
7. **Type Safety**: Strong typing with validation schemas

## Build Configuration

Requires LVGL configured with calendar support. In `lv_conf.h`:

```c
#define LV_USE_CALENDAR 1
```

ESPHome will automatically enable this when the calendar widget is used.

## Notes

- The calendar widget is based on LVGL v9.4 API
- Date validation ensures valid date ranges
- Highlighted dates are stored as static arrays for efficiency
- The widget inherits from `LvCompound` to support complex value types
- Month navigation is handled by LVGL internally
- Date selection triggers `on_value` event with year, month, day

## Troubleshooting

### Calendar not displaying
- Ensure `LV_USE_CALENDAR` is enabled in LVGL configuration
- Check that width and height are sufficient (minimum 200x200 recommended)

### Dates not highlighting
- Verify date values are within valid ranges
- Check that `highlighted_dates` is a list, even for single dates

### on_value not triggering
- Ensure the calendar has proper touch/input configuration
- Verify the widget is not overlapped by other widgets

## Future Enhancements

Potential improvements:
- Header button customization (month/year selectors)
- Month name localization
- Week number display
- Multi-date selection mode
- Date range selection
- Custom day name headers
