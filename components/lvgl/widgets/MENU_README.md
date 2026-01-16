# LVGL Menu Widget Implementation for ESPHome

## Overview

This implementation provides full support for the LVGL v9.4 Menu widget (`lv_menu`) in ESPHome. The menu widget enables hierarchical navigation with multiple pages, headers, back buttons, and sidebar support.

## File Location

`/home/user/test2_esp_video_esphome/components/lvgl/widgets/menu.py`

## Implementation Details

### LVGL v9.4 API Support

The implementation uses the following LVGL v9.4 API functions:

1. **`lv_menu_create(parent)`** - Creates the menu widget
2. **`lv_menu_page_create(menu, title)`** - Creates a menu page with title
3. **`lv_menu_set_mode_root_back_btn(menu, mode)`** - Show/hide back button on root page
4. **`lv_menu_set_sidebar_page(menu, page)`** - Sets sidebar page
5. **`lv_menu_set_page(menu, page)`** - Navigate to a specific page
6. **`lv_menu_back(menu)`** - Navigate back one level

### Menu Features

- **Hierarchical Navigation** - Multi-level page navigation
- **Header** - Title bar with back button
- **Sidebar** - Optional sidebar panel
- **Breadcrumb Trail** - Navigation history
- **Root Back Button** - Configurable behavior for root level

### Widget Parts

The menu widget supports three parts for styling:

1. **MAIN** - Main menu container
2. **HEADER** - Header/title bar area
3. **SIDEBAR** - Sidebar panel area

## Configuration Schema

### Menu Parameters

- **`pages`** (list): List of menu page configurations
- **`root_back_button`** (boolean, default: false): Show back button on root page
- **`mode`** (enum, default: HEADER): Menu header mode
  - ROOT, HEADER, SIDEBAR
- **`sidebar_page`** (id): Reference to page used as sidebar

### Page Parameters

- **`id`** (required): Page identifier for navigation
- **`title`** (string): Page title text
- **`widgets`** (list): Child widgets for page content
- **`bg_color`** (color): Background color
- **Other styling properties**: All standard widget properties

## Usage Examples

### Basic Menu

```yaml
lvgl:
  - menu:
      id: main_menu
      x: 0
      y: 0
      width: 100%
      height: 100%
      root_back_button: false
      pages:
        - id: main_page
          title: "Main Menu"
          widgets:
            - label:
                text: "Select an option"
            - button:
                text: "Settings"
            - button:
                text: "About"

        - id: settings_page
          title: "Settings"
          widgets:
            - label:
                text: "Settings Options"
            - switch:
                text: "WiFi"
            - switch:
                text: "Bluetooth"
```

### Multi-Level Menu

```yaml
lvgl:
  - menu:
      id: settings_menu
      pages:
        # Level 1: Main menu
        - id: root_page
          title: "Settings"
          widgets:
            - list:
                items:
                  - type: button
                    text: "Network"
                  - type: button
                    text: "Display"
                  - type: button
                    text: "System"

        # Level 2: Network settings
        - id: network_page
          title: "Network Settings"
          widgets:
            - label:
                text: "WiFi Configuration"
            - text_area:
                placeholder: "SSID"
            - text_area:
                placeholder: "Password"

        # Level 2: Display settings
        - id: display_page
          title: "Display Settings"
          widgets:
            - label:
                text: "Brightness"
            - slider:
                min_value: 0
                max_value: 100

        # Level 2: System settings
        - id: system_page
          title: "System Info"
          widgets:
            - label:
                text: "Version: 1.0.0"
```

### Menu with Sidebar

```yaml
lvgl:
  - menu:
      id: app_menu
      sidebar_page: sidebar
      pages:
        # Sidebar page
        - id: sidebar
          widgets:
            - list:
                items:
                  - type: button
                    text: "Home"
                  - type: button
                    text: "Data"
                  - type: button
                    text: "Settings"

        # Content pages
        - id: home_page
          title: "Home"
          widgets:
            - label:
                text: "Welcome to Home"

        - id: data_page
          title: "Data"
          widgets:
            - chart:
                id: data_chart
                type: LINE

        - id: settings_page
          title: "Settings"
          widgets:
            - label:
                text: "App Settings"
```

## Navigation Actions

Navigate between pages using actions:

```yaml
# Navigate to a specific page
on_...:
  then:
    - lvgl.menu.set_page:
        id: main_menu
        page: settings_page

# Navigate back one level
on_...:
  then:
    - lvgl.menu.back:
        id: main_menu

# Navigate to root page
on_...:
  then:
    - lvgl.menu.clear_history:
        id: main_menu
```

## Integration Examples

### Settings Menu with Real Controls

```yaml
lvgl:
  - menu:
      id: device_menu
      pages:
        - id: main_settings
          title: "Device Settings"
          widgets:
            - button:
                id: wifi_btn
                text: "WiFi Settings"
                on_click:
                  - lvgl.menu.set_page:
                      id: device_menu
                      page: wifi_settings

            - button:
                text: "Display Settings"
                on_click:
                  - lvgl.menu.set_page:
                      id: device_menu
                      page: display_settings

        - id: wifi_settings
          title: "WiFi"
          widgets:
            - switch:
                id: wifi_switch
                text: "Enable WiFi"
                on_value:
                  then:
                    - lambda: |-
                        if (x) {
                          // Enable WiFi
                        } else {
                          // Disable WiFi
                        }

        - id: display_settings
          title: "Display"
          widgets:
            - label:
                text: "Brightness:"
            - slider:
                id: brightness_slider
                min_value: 0
                max_value: 100
                on_value:
                  then:
                    - lambda: |-
                        // Set display brightness
```

## Features

### ✅ Implemented

- ✅ Create menu widget
- ✅ Create menu pages with titles
- ✅ Multi-level page hierarchy
- ✅ Root back button configuration
- ✅ Sidebar support
- ✅ Header styling
- ✅ Page content widgets
- ✅ Navigation actions
- ✅ MAIN, HEADER, SIDEBAR parts

### 🎯 Key Benefits

1. **Hierarchical Navigation** - Organize settings and options in levels
2. **Professional UI** - Standard menu patterns with headers
3. **Flexible Layout** - Optional sidebar and custom page content
4. **Easy Navigation** - Automatic breadcrumb and back button handling
5. **Styleable** - Full styling control for all parts

## Code Generation

The implementation generates C++ code that calls LVGL functions:

```cpp
// Example generated code
lv_obj_t* menu = lv_menu_create(parent);
lv_obj_t* page1 = lv_menu_page_create(menu, "Settings");
lv_obj_t* page2 = lv_menu_page_create(menu, "Network");
lv_menu_set_mode_root_back_btn(menu, LV_MENU_ROOT_BACK_BTN_DISABLED);
lv_menu_set_sidebar_page(menu, sidebar_page);
lv_menu_set_page(menu, page1);
```

## Dependencies

The menu widget uses button and label components:

```python
def get_uses(self):
    return ("btn", "label")
```

## LVGL Component Requirement

The implementation requires the LVGL MENU component to be enabled:

```python
lvgl_components_required.add("menu")
```

## Technical Notes

### Pattern Compliance

The implementation follows established patterns from:
- **tabview.py** - Multi-page navigation pattern
- **msgbox.py** - Header and content areas
- **list.py** - Item-based navigation

### Type Safety

- Uses `LvType` for C++ type definitions
- Validates configuration with `cv.Schema`
- Processes values with `lv_text.process()` and `lv_bool.process()`

## Use Cases

1. **Settings Menus** - Organize device settings hierarchically
2. **Application Navigation** - Multi-screen app navigation
3. **Configuration Wizards** - Step-by-step setup flows
4. **Dashboard Navigation** - Navigate between different views
5. **Context Menus** - Hierarchical option selection

## Best Practices

1. **Clear Titles** - Use descriptive page titles
2. **Logical Hierarchy** - Organize related items together
3. **Back Button** - Keep navigation consistent
4. **Page IDs** - Use meaningful identifiers
5. **Content Widgets** - Keep pages focused and simple

## References

- LVGL v9.4 Menu Documentation: https://docs.lvgl.io/9.4/widgets/menu.html
- ESPHome LVGL Integration: https://esphome.io/components/lvgl/
