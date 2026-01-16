# LVGL List Widget - Quick Reference

## Basic Usage

```yaml
list:
  id: my_list
  items:
    - type: text
      text: "Header"
    - type: button
      text: "Button 1"
    - type: button
      text: "Button 2"
```

## With Icons

```yaml
list:
  id: icon_list
  items:
    - type: button
      text: "Wi-Fi"
      src: wifi_icon
    - type: button
      text: "Settings"
      src: settings_icon
```

## Styling

```yaml
list:
  id: styled_list
  width: 200
  height: 300
  bg_color: 0xFFFFFF
  border_width: 2
  border_color: 0x333333
  pad_all: 5
  scrollbar:
    bg_color: 0xCCCCCC
    width: 8
```

## Dynamic Updates

```yaml
on_...:
  - lvgl.list.update:
      id: my_list
      items:
        - type: text
          text: "New Header"
        - type: button
          text: "New Button"
```

## With Templates

```yaml
list:
  id: template_list
  items:
    - type: button
      text: !lambda |-
        return "Status: " + std::string(id(sensor).state ? "ON" : "OFF");
```

## Item Types

| Type | Description | Properties |
|------|-------------|------------|
| `text` | Static text label | `text` (required) |
| `button` | Interactive button | `text` (required), `src` (optional icon) |

## Parts

| Part | Description | Customizable Properties |
|------|-------------|------------------------|
| `main` | Main container | All standard obj properties |
| `scrollbar` | Scrollbar | width, color, opacity, etc. |

## Common Properties

```yaml
list:
  # Size and position
  x: 10
  y: 10
  width: 200
  height: 300

  # Background
  bg_color: 0xFFFFFF
  bg_opa: 255

  # Border
  border_width: 2
  border_color: 0x333333

  # Padding
  pad_all: 5
  # or individually:
  # pad_top: 5
  # pad_bottom: 5
  # pad_left: 5
  # pad_right: 5

  # Scrollbar
  scrollbar:
    bg_color: 0xCCCCCC
    width: 8
```

## Tips

1. **Scrolling**: Automatically enabled when content exceeds container height
2. **Icons**: Must be defined as LVGL images before use
3. **Text**: Supports static strings, templates, and lambdas
4. **Buttons**: Individual button events handled via LVGL event system
5. **Performance**: Consider using `lvgl.list.update` for dynamic content

## Example: Complete Menu

```yaml
list:
  id: main_menu
  x: 10
  y: 10
  width: 300
  height: 400

  items:
    # Header
    - type: text
      text: "Main Menu"

    # Settings section
    - type: button
      text: "Wi-Fi Settings"
      src: wifi_icon
    - type: button
      text: "Display Settings"
      src: display_icon
    - type: button
      text: "Sound Settings"
      src: sound_icon

    # Divider
    - type: text
      text: "System"

    # System actions
    - type: button
      text: "About"
    - type: button
      text: "Restart"
    - type: button
      text: "Factory Reset"

  # Styling
  bg_color: 0xF5F5F5
  border_width: 1
  border_color: 0xDDDDDD
  radius: 10
  pad_all: 10

  scrollbar:
    bg_color: 0xBBBBBB
    radius: 4
    width: 6
```

## Related Actions

- `lvgl.list.update` - Update list items
- Standard LVGL actions (hide, show, etc.) also work

## Dependencies

Automatically included:
- `btn` (button widget)
- `label` (label widget)

## API Reference

LVGL functions used internally:
- `lv_list_create(parent)` - Create list
- `lv_list_add_button(list, icon, text)` - Add button
- `lv_list_add_text(list, text)` - Add text

## File Location

Implementation: `/components/lvgl/widgets/list.py`
