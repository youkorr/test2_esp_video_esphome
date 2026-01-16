# LVGL Table Widget Implementation for ESPHome

## Overview

This implementation provides full support for the LVGL v9.4 Table widget (`lv_table`) in ESPHome. The table widget displays data in a grid format with configurable rows, columns, and cell content.

## File Location

`/home/user/test2_esp_video_esphome/components/lvgl/widgets/table.py`

## Implementation Details

### LVGL v9.4 API Support

The implementation uses the following LVGL v9.4 API functions:

1. **`lv_table_create(parent)`** - Creates the table widget (handled automatically by the framework)
2. **`lv_table_set_row_count(obj, row_count)`** - Sets the number of rows
3. **`lv_table_set_column_count(obj, column_count)`** - Sets the number of columns
4. **`lv_table_set_cell_value(obj, row, col, text)`** - Sets the text content of a cell
5. **`lv_table_set_column_width(obj, col_id, width)`** - Sets the width of a column

### Architecture

The implementation follows the standard ESPHome LVGL widget pattern:

```python
class TableType(WidgetType):
    - Extends WidgetType base class
    - Registers widget type "table"
    - Defines supported parts: MAIN, ITEMS
    - Implements schema validation
    - Generates C++ code via to_code() method
```

### Widget Parts

The table widget supports two parts for styling:

1. **MAIN** - The table background and overall container
2. **ITEMS** - Individual cells in the table

### Configuration Schema

#### Required Parameters
None - all parameters are optional

#### Optional Parameters

- **`row_count`** (positive_int): Number of rows in the table
- **`column_count`** (positive_int): Number of columns in the table
- **`cells`** (list): List of cell configurations
  - `row` (positive_int, required): Row index (0-based)
  - `column` (positive_int, required): Column index (0-based)
  - `text` (string, required): Cell text content
- **`columns`** (list): List of column width configurations
  - `id` (positive_int, required): Column index (0-based)
  - `width` (positive_int, required): Column width in pixels

## Usage Examples

### Basic Table

```yaml
lvgl:
  - table:
      id: my_table
      row_count: 3
      column_count: 2
      cells:
        - row: 0
          column: 0
          text: "Name"
        - row: 0
          column: 1
          text: "Value"
        - row: 1
          column: 0
          text: "Temperature"
        - row: 1
          column: 1
          text: "25°C"
```

### Table with Column Widths

```yaml
lvgl:
  - table:
      id: sensor_table
      row_count: 5
      column_count: 3
      columns:
        - id: 0
          width: 100
        - id: 1
          width: 150
        - id: 2
          width: 80
      cells:
        - row: 0
          column: 0
          text: "Sensor"
        - row: 0
          column: 1
          text: "Reading"
        - row: 0
          column: 2
          text: "Unit"
```

### Styled Table

```yaml
lvgl:
  - table:
      id: styled_table
      row_count: 4
      column_count: 3
      # Main part styling (table background)
      styles:
        - style:
            bg_color: 0xFFFFFF
            border_width: 2
            border_color: 0x333333
            pad_all: 10
      # Items part styling (cells)
      items:
        styles:
          - style:
              text_color: 0x000000
              text_font: roboto_16
              pad_all: 5
              border_width: 1
              border_color: 0xCCCCCC
      cells:
        - row: 0
          column: 0
          text: "Header 1"
        - row: 0
          column: 1
          text: "Header 2"
```

### Dynamic Updates

You can update table cells dynamically using the update action:

```yaml
# In your automations or scripts
on_...:
  - lvgl.table.update:
      id: my_table
      cells:
        - row: 1
          column: 1
          text: !lambda 'return to_string(id(temperature_sensor).state);'
```

## Features

### ✅ Implemented

- ✅ Create table widget
- ✅ Set row count
- ✅ Set column count
- ✅ Set cell text values
- ✅ Configure column widths
- ✅ Support for MAIN and ITEMS parts
- ✅ Cell styling through parts
- ✅ Dynamic updates via lvgl.table.update action
- ✅ Integration with ESPHome templating system
- ✅ Lambda support for dynamic values

### 🎯 Key Benefits

1. **Grid Layout** - Organize data in rows and columns
2. **Configurable** - Set custom column widths and cell content
3. **Styleable** - Full styling support for table and cells
4. **Dynamic** - Update cell values at runtime
5. **Scrollable** - Automatic scrolling for large tables (LVGL default behavior)

## Code Generation

The implementation generates C++ code that calls LVGL functions:

```cpp
// Example generated code
lv_table_set_row_count(obj, 5);
lv_table_set_column_count(obj, 3);
lv_table_set_column_width(obj, 0, 100);
lv_table_set_column_width(obj, 1, 150);
lv_table_set_cell_value(obj, 0, 0, "Header");
```

## Integration

The widget is automatically registered when the file is present in the `widgets/` directory due to the dynamic module loading in `__init__.py`:

```python
for module_info in pkgutil.iter_modules(widgets.__path__):
    importlib.import_module(f".widgets.{module_info.name}", package=__package__)
```

The `TableType.__init__()` constructor automatically registers the widget type in the global `WIDGET_TYPES` dictionary and creates the update action `lvgl.table.update`.

## Dependencies

The table widget uses the label widget for rendering text in cells:

```python
def get_uses(self):
    return ("label",)
```

This ensures that the label component is included when tables are used.

## LVGL Component Requirement

The implementation requires the LVGL TABLE component to be enabled:

```python
lvgl_components_required.add("TABLE")
```

## Technical Notes

### Pattern Compliance

The implementation follows established patterns from:
- **buttonmatrix.py** - Grid/matrix widget pattern
- **label.py** - Text handling pattern
- **dropdown.py** - List-based configuration pattern

### Type Safety

- Uses `LvType` for C++ type definitions
- Validates configuration with `cv.Schema`
- Processes values with `lv_int.process()` and `lv_text.process()`

### Code Generation

- Uses `lv.table_xxx()` for LVGL function calls
- Leverages `MockLv` for automatic code context management
- Supports async processing for lambda expressions

## Testing

To test the implementation:

1. Add a table widget to your LVGL configuration
2. Verify the ESPHome compilation succeeds
3. Check the generated C++ code includes proper `lv_table_*` calls
4. Test on actual hardware to verify display output
5. Test dynamic updates via actions

## Future Enhancements

Potential additions (not currently implemented):

- Cell merging/spanning
- Cell text alignment per cell
- Cell control flags (e.g., crop, right-aligned)
- Row/column selection
- Cell click events
- Header row configuration
- Cell value formatting options

## References

- LVGL v9.4 Table Documentation: https://docs.lvgl.io/9.4/widgets/table.html
- ESPHome LVGL Integration: https://esphome.io/components/lvgl/
- Implementation based on ESPHome widget patterns
