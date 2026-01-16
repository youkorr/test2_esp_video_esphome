# LVGL Window Widget Implementation for ESPHome

## Overview

This implementation provides full support for the LVGL v9.4 Window widget (`lv_win`) in ESPHome. The window widget creates modal windows with a title bar, content area, and optional header buttons.

## File Location

`/home/user/test2_esp_video_esphome/components/lvgl/widgets/win.py`

## Implementation Details

### LVGL v9.4 API Support

The implementation uses the following LVGL v9.4 API functions:

1. **`lv_win_create(parent)`** - Creates the window widget
2. **`lv_win_add_title(win, title)`** - Sets the window title
3. **`lv_win_add_button(win, icon, width)`** - Adds button to header
4. **`lv_win_get_header(win)`** - Gets the header object
5. **`lv_win_get_content(win)`** - Gets the content area object

### Window Features

- **Title Bar** - Header with customizable title
- **Header Buttons** - Add buttons with icons (close, minimize, etc.)
- **Content Area** - Main area for widgets
- **Draggable** - Can be moved by dragging header
- **Modal** - Can be used as modal dialogs

### Widget Parts

The window widget supports three parts for styling:

1. **MAIN** - Window container/background
2. **HEADER** - Title bar area
3. **BODY** - Content area

## Configuration Schema

### Window Parameters

- **`title`** (string, required): Window title text
- **`header_height`** (int, default: 40): Height of title bar in pixels
- **`header_buttons`** (list): List of header button configurations
- **`widgets`** (list): Child widgets for content area

### Header Button Parameters

- **`id`** (optional): Button identifier for events
- **`src`** (optional): Icon image for button

## Usage Examples

### Basic Window

```yaml
lvgl:
  - win:
      id: info_window
      title: "Information"
      x: 50
      y: 50
      width: 300
      height: 200
      bg_color: 0xFFFFFF
      border_width: 2
      widgets:
        - label:
            text: "This is a window widget"
            align: CENTER
```

### Window with Header Buttons

```yaml
lvgl:
  - win:
      id: settings_window
      title: "Settings"
      x: 100
      y: 100
      width: 400
      height: 300
      header_height: 50

      # Style the header
      header:
        bg_color: 0x0080FF
        text_color: 0xFFFFFF

      # Add header buttons
      header_buttons:
        - id: close_btn
          src: close_icon
        - id: minimize_btn
          src: minimize_icon

      # Content area
      widgets:
        - label:
            text: "Settings Content"
            x: 10
            y: 10
        - switch:
            text: "Enable Feature"
            x: 10
            y: 50
```

### Modal Dialog Window

```yaml
lvgl:
  - win:
      id: confirm_dialog
      title: "Confirm Action"
      x: CENTER
      y: CENTER
      width: 300
      height: 150
      bg_color: 0xFFFFFF
      shadow_width: 20
      shadow_color: 0x000000
      shadow_opa: 50%

      widgets:
        - label:
            text: "Are you sure you want to proceed?"
            align: TOP_MID
            y: 10

        - container:
            layout: flex
            flex_flow: ROW
            align: BOTTOM_MID
            y: -10
            widgets:
              - button:
                  id: cancel_btn
                  text: "Cancel"
                  width: 100
              - button:
                  id: ok_btn
                  text: "OK"
                  width: 100
                  bg_color: 0x0080FF
```

### File Browser Window

```yaml
lvgl:
  - win:
      id: file_browser
      title: "Select File"
      x: 50
      y: 50
      width: 400
      height: 350

      header_buttons:
        - id: close_file_browser
          src: close_icon

      widgets:
        # File list
        - list:
            id: file_list
            x: 5
            y: 5
            width: 380
            height: 250
            items:
              - type: text
                text: "Documents"
              - type: button
                text: "file1.txt"
              - type: button
                text: "file2.txt"
              - type: button
                text: "file3.txt"

        # Action buttons
        - button:
            text: "Open"
            x: 250
            y: 270
            width: 130
            bg_color: 0x00AA00
```

## Window Actions

### Show/Hide Window

```yaml
# Show window
on_...:
  then:
    - lvgl.obj.clear_flag:
        id: info_window
        flag: HIDDEN

# Hide window
on_...:
  then:
    - lvgl.obj.add_flag:
        id: info_window
        flag: HIDDEN
```

### Update Window Title

```yaml
on_...:
  then:
    - lambda: |-
        // Update window title
        lv_win_add_title(id(info_window), "New Title");
```

### Close Button Handler

```yaml
# In header button configuration
header_buttons:
  - id: close_btn
    src: close_icon
    on_click:
      then:
        - lvgl.obj.add_flag:
            id: settings_window
            flag: HIDDEN
```

## Features

### ✅ Implemented

- ✅ Create window widget
- ✅ Set window title
- ✅ Configure header height
- ✅ Add header buttons with icons
- ✅ Content area for widgets
- ✅ MAIN, HEADER, BODY parts styling
- ✅ Modal window support
- ✅ Draggable header

### 🎯 Key Benefits

1. **Professional UI** - Standard window appearance
2. **Flexible Layout** - Custom content and header buttons
3. **Modal Dialogs** - User confirmations and inputs
4. **Organized Content** - Clear separation of header and body
5. **Interactive** - Draggable and closeable windows

## Code Generation

The implementation generates C++ code that calls LVGL functions:

```cpp
// Example generated code
lv_obj_t* win = lv_win_create(parent);
lv_win_add_title(win, "Settings");
lv_obj_t* close_btn = lv_win_add_button(win, close_icon, 40);
lv_obj_t* content = lv_win_get_content(win);
// Add widgets to content area
```

## Dependencies

The window widget uses button and label components:

```python
def get_uses(self):
    return ("btn", "label")
```

## LVGL Component Requirement

The implementation requires the LVGL WIN component to be enabled:

```python
lvgl_components_required.add("win")
```

## Technical Notes

### Pattern Compliance

The implementation follows established patterns from:
- **msgbox.py** - Modal window and button patterns
- **tabview.py** - Multi-area widget management
- **obj.py** - Container and child widget handling

### Type Safety

- Uses `LvType` for C++ type definitions
- Validates configuration with `cv.Schema`
- Processes values with `lv_text.process()` and `lv_image.process()`

## Use Cases

1. **Settings Dialogs** - Configuration windows
2. **Confirmation Dialogs** - User confirmations
3. **File Browsers** - File/folder selection
4. **Information Windows** - Display detailed information
5. **Property Editors** - Edit object properties
6. **Wizards** - Multi-step configuration

## Best Practices

1. **Clear Titles** - Use descriptive window titles
2. **Header Buttons** - Provide close/minimize buttons
3. **Content Layout** - Organize content clearly
4. **Modal Usage** - Use for important dialogs
5. **Consistent Sizing** - Use appropriate window dimensions
6. **Shadow Effects** - Add shadows for modal windows

## Integration Example

```yaml
# Complete settings dialog example
lvgl:
  - win:
      id: device_settings
      title: "Device Settings"
      x: CENTER
      y: CENTER
      width: 350
      height: 280
      bg_color: 0xFFFFFF
      shadow_width: 15

      header:
        bg_color: 0x2196F3
        text_color: 0xFFFFFF
        pad_all: 8

      header_buttons:
        - id: settings_close
          src: close_icon
          on_click:
            - lvgl.obj.add_flag:
                id: device_settings
                flag: HIDDEN

      body:
        pad_all: 10

      widgets:
        - label:
            text: "Device Name:"
            x: 5
            y: 5

        - text_area:
            id: device_name
            x: 5
            y: 30
            width: 320
            placeholder: "Enter device name"

        - label:
            text: "Language:"
            x: 5
            y: 80

        - dropdown:
            id: language_select
            x: 5
            y: 105
            width: 150
            options:
              - "English"
              - "Français"
              - "Deutsch"
              - "Español"

        - button:
            text: "Save"
            x: 200
            y: 200
            width: 120
            bg_color: 0x4CAF50
            text_color: 0xFFFFFF
            on_click:
              - lambda: |-
                  // Save settings
              - lvgl.obj.add_flag:
                  id: device_settings
                  flag: HIDDEN

# Show dialog on button press
on_button_press:
  - lvgl.obj.clear_flag:
      id: device_settings
      flag: HIDDEN
```

## References

- LVGL v9.4 Window Documentation: https://docs.lvgl.io/9.4/widgets/win.html
- ESPHome LVGL Integration: https://esphome.io/components/lvgl/
