# LVGL v9.4 List Widget Implementation for ESPHome

## Overview

This implementation provides full support for the LVGL v9.4 List widget (`lv_list`) in ESPHome, following the established patterns from other widgets like `dropdown` and `button`.

## File Location

`/home/user/test2_esp_video_esphome/components/lvgl/widgets/list.py`

## Implementation Details

### Widget Type

- **Name**: `list`
- **Type**: `lv_list_t` (LvCompound)
- **Parts**: `MAIN`, `SCROLLBAR`
- **Dependencies**: `btn`, `label`

### LVGL v9.4 API Functions Used

1. `lv_list_create(parent)` - Creates the list widget (automatically called by ESPHome framework)
2. `lv_list_add_button(list, icon, text)` - Adds a button to the list
3. `lv_list_add_text(list, text)` - Adds a text item to the list

### Configuration Schema

#### List Items

The `items` property accepts a list of items, where each item has:

- **type** (required): Either `"button"` or `"text"`
- **text** (required): The text content (supports templating and lambdas)
- **src** (optional): Icon/image for button items (LVGL image source)

## Usage Examples

### Basic List

```yaml
list:
  id: my_list
  items:
    - type: text
      text: "Section Header"
    - type: button
      text: "Option 1"
    - type: button
      text: "Option 2"
```

### List with Icons

```yaml
list:
  id: settings_list
  items:
    - type: button
      text: "Wi-Fi Settings"
      src: wifi_icon  # Reference to an LVGL image
    - type: button
      text: "Display Settings"
      src: display_icon
```

### List with Styling

```yaml
list:
  id: styled_list
  width: 200
  height: 300
  bg_color: 0xFFFFFF
  border_width: 2
  border_color: 0x333333
  pad_all: 5

  # Style the scrollbar part
  scrollbar:
    bg_color: 0xCCCCCC
    width: 8

  items:
    - type: text
      text: "Menu"
    - type: button
      text: "Settings"
```

### Dynamic Updates

The widget supports dynamic updates via the `lvgl.list.update` action:

```yaml
on_some_event:
  - lvgl.list.update:
      id: my_list
      items:
        - type: text
          text: "Updated Menu"
        - type: button
          text: "New Option"
```

### Templating Support

Text values support ESPHome templating:

```yaml
list:
  id: dynamic_list
  items:
    - type: text
      text: "Device Status"
    - type: button
      text: !lambda |-
        return id(wifi_connected) ? "Disconnect" : "Connect";
```

## Features

### Scrolling

The list widget automatically enables scrolling when content exceeds the container height. The `SCROLLBAR` part allows customization of the scrollbar appearance.

### Flex Layout

The list uses LVGL's flex layout internally to arrange items vertically with proper spacing.

### Event Handling

Button items in the list can be configured to respond to events (clicks, long presses, etc.) through LVGL's event system. Individual buttons can be styled and configured with different properties.

## Parts and States

### MAIN Part
- The main container of the list
- Supports all standard obj properties (background, border, padding, etc.)

### SCROLLBAR Part
- The scrollbar that appears when content is scrollable
- Can be styled independently (width, color, opacity, etc.)

## Technical Implementation Notes

### Code Generation

The implementation follows ESPHome's code generation patterns:

1. **Widget Registration**: Automatically registered via `WidgetType.__init__` when the module is imported
2. **Code Generation**: Uses `lv.list_add_button()` and `lv.list_add_text()` for direct LVGL API calls
3. **Text Processing**: Uses `lv_text.process()` for proper text handling (including lambdas and templates)
4. **Image Processing**: Uses `lv_image.process()` for icon handling

### Key Design Decisions

1. **Icon Parameter**: When no icon is specified for buttons, `NULL` is passed to maintain LVGL API compatibility
2. **Item Types**: Clear distinction between "button" (interactive) and "text" (static label) items
3. **Schema Validation**: Uses `cv.one_of()` to ensure item type is valid
4. **Component Dependencies**: Declares dependencies on `btn` and `label` widgets via `get_uses()`

## Comparison with Reference Widgets

### Similarities to Dropdown
- Uses list-based items configuration
- Supports text processing with `lv_text`
- Has MAIN and SCROLLBAR parts

### Similarities to Button
- Button items follow button widget patterns
- Supports text configuration
- Can contain child elements (icons)

### Similarities to Msgbox
- Uses add functions for dynamic content (`lv.list_add_button`, similar to `lv.msgbox_add_footer_button`)
- Supports optional icons for buttons

## Files Modified/Created

### Created
- `/home/user/test2_esp_video_esphome/components/lvgl/widgets/list.py` - Main implementation

### Auto-Loaded
- Widget is automatically imported and registered via `pkgutil.iter_modules()` in `/home/user/test2_esp_video_esphome/components/lvgl/__init__.py`

## Testing

To test the implementation:

1. Include the list widget in your LVGL configuration
2. Compile the ESPHome firmware
3. Verify that:
   - The list is created correctly
   - Items appear in the correct order
   - Buttons are clickable
   - Text items are displayed properly
   - Icons appear when specified
   - Scrolling works when content exceeds container height
   - Styling applies correctly to both MAIN and SCROLLBAR parts

## Future Enhancements

Possible future improvements:

1. **Button Event Handlers**: Add support for individual button event handlers
2. **Item IDs**: Allow assigning IDs to individual list items for targeted updates
3. **Checkboxes**: Support checkbox list items
4. **Custom Widgets**: Allow arbitrary widgets as list items
5. **Animation**: Support animated item addition/removal

## Related Documentation

- [LVGL v9.4 List Documentation](https://docs.lvgl.io/master/widgets/list.html)
- ESPHome LVGL Integration: See other widgets in `/components/lvgl/widgets/`
- Base widget patterns: `/components/lvgl/widgets/__init__.py`
