# LVGL Image Button Widget Implementation for ESPHome

## Overview

This implementation provides full support for the LVGL v9.4 Image Button widget (`lv_imagebutton`) in ESPHome. The Image Button widget creates buttons that display different images based on their state (released, pressed, disabled, checked).

## File Location

`/home/user/test2_esp_video_esphome/components/lvgl/widgets/imgbtn.py`

## Implementation Details

### LVGL v9.4 API Support

The implementation uses the following LVGL v9.4 API functions:

1. **`lv_imagebutton_create(parent)`** - Creates the image button widget
2. **`lv_imagebutton_set_src(obj, state, src_left, src_mid, src_right)`** - Sets images for different button states

### Button States

The image button supports six different states:

1. **RELEASED** - Normal/idle state
2. **PRESSED** - Button is being pressed
3. **DISABLED** - Button is disabled/inactive
4. **CHECKED_RELEASED** - Checked state (not pressed)
5. **CHECKED_PRESSED** - Checked state (being pressed)
6. **CHECKED_DISABLED** - Checked but disabled

## Configuration Schema

### Widget Parameters

- **`src_released`** (image, optional): Image for released state
- **`src_pressed`** (image, optional): Image for pressed state
- **`src_disabled`** (image, optional): Image for disabled state
- **`src_checked_released`** (image, optional): Image for checked+released state
- **`src_checked_pressed`** (image, optional): Image for checked+pressed state
- **`src_checked_disabled`** (image, optional): Image for checked+disabled state

All standard widget properties (position, size, etc.) are supported.

## Usage Examples

### Basic Image Button

```yaml
lvgl:
  - imgbtn:
      id: power_button
      x: 50
      y: 50
      src_released: power_off_icon
      src_pressed: power_off_pressed_icon
      on_click:
        then:
          - lambda: |-
              ESP_LOGI("button", "Power button clicked");
```

### Toggle Button with States

```yaml
lvgl:
  - imgbtn:
      id: wifi_toggle
      x: 100
      y: 100
      # Normal states (WiFi OFF)
      src_released: wifi_off_icon
      src_pressed: wifi_off_pressed_icon
      # Checked states (WiFi ON)
      src_checked_released: wifi_on_icon
      src_checked_pressed: wifi_on_pressed_icon
      # Disabled states
      src_disabled: wifi_disabled_icon
      src_checked_disabled: wifi_on_disabled_icon
      on_click:
        then:
          - lambda: |-
              // Toggle WiFi state
              bool current_state = lv_obj_has_state(id(wifi_toggle), LV_STATE_CHECKED);
              if (current_state) {
                lv_obj_clear_state(id(wifi_toggle), LV_STATE_CHECKED);
                // Turn WiFi OFF
              } else {
                lv_obj_add_state(id(wifi_toggle), LV_STATE_CHECKED);
                // Turn WiFi ON
              }
```

### Media Player Controls

```yaml
lvgl:
  pages:
    - id: media_page
      widgets:
        # Play/Pause button
        - imgbtn:
            id: play_pause_btn
            x: CENTER
            y: 300
            src_released: play_icon
            src_pressed: play_pressed_icon
            src_checked_released: pause_icon
            src_checked_pressed: pause_pressed_icon
            on_click:
              then:
                - lambda: |-
                    // Toggle play/pause

        # Previous button
        - imgbtn:
            id: prev_btn
            x: 100
            y: 300
            src_released: prev_icon
            src_pressed: prev_pressed_icon
            on_click:
              then:
                - lambda: |-
                    // Previous track

        # Next button
        - imgbtn:
            id: next_btn
            x: 400
            y: 300
            src_released: next_icon
            src_pressed: next_pressed_icon
            on_click:
              then:
                - lambda: |-
                    // Next track

        # Volume buttons
        - imgbtn:
            id: vol_down_btn
            x: 50
            y: 400
            src_released: volume_down_icon
            src_pressed: volume_down_pressed_icon

        - imgbtn:
            id: vol_up_btn
            x: 450
            y: 400
            src_released: volume_up_icon
            src_pressed: volume_up_pressed_icon
```

### Settings Toggles

```yaml
lvgl:
  - container:
      x: 20
      y: 20
      width: 460
      height: 400
      widgets:
        # WiFi toggle
        - label:
            text: "WiFi"
            x: 10
            y: 20

        - imgbtn:
            id: wifi_btn
            x: 400
            y: 10
            src_released: toggle_off_icon
            src_checked_released: toggle_on_icon

        # Bluetooth toggle
        - label:
            text: "Bluetooth"
            x: 10
            y: 80

        - imgbtn:
            id: bt_btn
            x: 400
            y: 70
            src_released: toggle_off_icon
            src_checked_released: toggle_on_icon

        # Airplane mode toggle
        - label:
            text: "Airplane Mode"
            x: 10
            y: 140

        - imgbtn:
            id: airplane_btn
            x: 400
            y: 130
            src_released: toggle_off_icon
            src_checked_released: toggle_on_icon
```

### Custom Icon Buttons

```yaml
lvgl:
  - imgbtn:
      id: home_btn
      x: 50
      y: 50
      src_released: home_icon
      src_pressed: home_pressed_icon
      on_click:
        - lvgl.page.show:
            id: home_page

  - imgbtn:
      id: settings_btn
      x: 150
      y: 50
      src_released: settings_icon
      src_pressed: settings_pressed_icon
      src_disabled: settings_disabled_icon
      on_click:
        - lvgl.page.show:
            id: settings_page

  - imgbtn:
      id: info_btn
      x: 250
      y: 50
      src_released: info_icon
      src_pressed: info_pressed_icon
      on_click:
        - lvgl.obj.clear_flag:
            id: info_window
            flag: HIDDEN
```

## Features

### ✅ Implemented

- ✅ Six button states support
- ✅ Custom image per state
- ✅ Click/press/release triggers
- ✅ Checkable button mode
- ✅ Disabled state
- ✅ Standard widget properties

### 🎯 Key Benefits

1. **Visual Feedback** - Different images for each state
2. **Professional UI** - Custom icon designs
3. **Toggle Buttons** - Built-in checked state
4. **State Management** - Automatic state handling
5. **Flexible** - Any image format supported

## Image Requirements

### Supported Formats

- PNG (with transparency)
- JPG/JPEG
- BMP
- GIF (first frame)
- SVG (if ThorVG enabled)

### Recommendations

1. **Size**: Design icons at intended display size
2. **Format**: Use PNG with transparency for best results
3. **Optimization**: Compress images to reduce memory
4. **Consistency**: Use same size for all states
5. **DPI**: Match display resolution

### Example Image Sizes

- Small icons: 32x32 or 48x48
- Medium buttons: 64x64 or 80x80
- Large buttons: 100x100 or 128x128

## State Management

### Setting States

```yaml
# Make button checked
on_...:
  then:
    - lambda: |-
        lv_obj_add_state(id(my_imgbtn), LV_STATE_CHECKED);

# Clear checked state
on_...:
  then:
    - lambda: |-
        lv_obj_clear_state(id(my_imgbtn), LV_STATE_CHECKED);

# Disable button
on_...:
  then:
    - lambda: |-
        lv_obj_add_state(id(my_imgbtn), LV_STATE_DISABLED);
```

### Checking States

```yaml
on_click:
  then:
    - lambda: |-
        if (lv_obj_has_state(id(my_imgbtn), LV_STATE_CHECKED)) {
          ESP_LOGI("button", "Button is checked");
        } else {
          ESP_LOGI("button", "Button is not checked");
        }
```

## Triggers

### on_click

Triggered when button is clicked (press and release).

```yaml
on_click:
  then:
    - lambda: |-
        ESP_LOGI("button", "Clicked");
```

### on_press

Triggered when button is pressed down.

```yaml
on_press:
  then:
    - lambda: |-
        ESP_LOGI("button", "Pressed");
```

### on_release

Triggered when button is released.

```yaml
on_release:
  then:
    - lambda: |-
        ESP_LOGI("button", "Released");
```

## Code Generation

The implementation generates C++ code:

```cpp
// Example generated code
lv_obj_t* imgbtn = lv_imagebutton_create(parent);
lv_imagebutton_set_src(imgbtn, LV_IMGBTN_STATE_RELEASED, image_released, NULL, NULL);
lv_imagebutton_set_src(imgbtn, LV_IMGBTN_STATE_PRESSED, image_pressed, NULL, NULL);
lv_imagebutton_set_src(imgbtn, LV_IMGBTN_STATE_CHECKED_RELEASED, image_checked, NULL, NULL);
```

## Dependencies

The image button widget uses the image component:

```python
def get_uses(self):
    return ("img",)
```

## LVGL Component Requirement

```python
lvgl_components_required.add("imgbtn")
```

## Technical Notes

### Pattern Compliance

Follows ESPHome LVGL widget patterns:
- Standard button behavior
- Image validation and processing
- State management

### Memory Considerations

- Each image is stored in memory
- Use image compression when possible
- Consider using same image for multiple states
- Unload unused images

## Use Cases

1. **Media Controls** - Play, pause, skip buttons
2. **Toggle Switches** - ON/OFF indicators
3. **Navigation** - Home, back, menu buttons
4. **Settings** - WiFi, Bluetooth toggles
5. **Custom Icons** - Any icon-based button
6. **Status Indicators** - Visual state feedback

## Best Practices

1. **Consistent Sizing** - Use same size for all state images
2. **Visual Feedback** - Provide clear pressed state
3. **Accessibility** - Ensure sufficient contrast
4. **Performance** - Optimize image sizes
5. **Fallbacks** - Provide at least released and pressed states

## Example Integration

```yaml
# Complete media player example
image:
  - file: "icons/play.png"
    id: play_icon
  - file: "icons/play_pressed.png"
    id: play_pressed_icon
  - file: "icons/pause.png"
    id: pause_icon
  - file: "icons/pause_pressed.png"
    id: pause_pressed_icon

lvgl:
  - imgbtn:
      id: media_control
      x: CENTER
      y: CENTER
      src_released: play_icon
      src_pressed: play_pressed_icon
      src_checked_released: pause_icon
      src_checked_pressed: pause_pressed_icon
      on_click:
        then:
          - lambda: |-
              bool playing = lv_obj_has_state(id(media_control), LV_STATE_CHECKED);
              if (playing) {
                // Pause media
                id(media_player)->pause();
                lv_obj_clear_state(id(media_control), LV_STATE_CHECKED);
              } else {
                // Play media
                id(media_player)->play();
                lv_obj_add_state(id(media_control), LV_STATE_CHECKED);
              }
```

## References

- [LVGL v9.4 Image Button Documentation](https://docs.lvgl.io/9.4/details/widgets/imagebutton.html)
- [ESPHome Image Component](https://esphome.io/components/image.html)

---

**Implementation Status:** ✅ Complete
**LVGL Version:** 9.4.0
