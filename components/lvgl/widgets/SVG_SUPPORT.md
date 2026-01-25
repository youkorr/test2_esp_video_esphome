# SVG Support in LVGL 9.4 for ESPHome

## Overview

LVGL 9.4 provides native SVG support through the ThorVG vector graphics engine. SVG files can be used anywhere regular images are used, with the benefit of scalable vector graphics that look perfect at any size.

## Requirements

1. **LVGL 9.4** - SVG support is only available in LVGL v9+
2. **ThorVG** - The vector graphics rendering engine (built into LVGL 9.4)
3. **Build Configuration** - Enable `LV_USE_THORVG_INTERNAL` and `LV_USE_SVG`

## Configuration

### In your ESPHome YAML:

```yaml
# Enable LVGL advanced features with SVG support
lvgl_advanced_features:
  thorvg:
    internal: true   # Use built-in ThorVG
  svg: true          # Enable SVG support
```

## Usage

SVG images work with the standard `image` widget. Simply use an SVG file as the `src`:

### Basic SVG Image

```yaml
lvgl:
  pages:
    - id: main_page
      widgets:
        # SVG icon - scales perfectly
        - image:
            id: weather_icon
            src: "/sdcard/icons/sun.svg"
            x: 100
            y: 50
            width: 64
            height: 64
```

### Dynamically Changing SVG Icons

```yaml
# Weather icon that changes based on conditions
- image:
    id: weather_icon
    src: "/sdcard/icons/sun.svg"
    width: 128
    height: 128

# Lambda to change icon
on_...:
  then:
    - lambda: |-
        const char* weather_svg;
        if (id(weather_condition) == "sunny") {
          weather_svg = "/sdcard/icons/sun.svg";
        } else if (id(weather_condition) == "rainy") {
          weather_svg = "/sdcard/icons/rain.svg";
        } else if (id(weather_condition) == "cloudy") {
          weather_svg = "/sdcard/icons/cloud.svg";
        }
        // Update SVG source
        lv_image_set_src(id(weather_icon), weather_svg);
```

### SVG Background Images

```yaml
- obj:
    id: card
    width: 200
    height: 150
    bg_image_src: "/sdcard/backgrounds/pattern.svg"
```

## SVG File Requirements

### File Format
- **Extension**: `.svg`
- **Version**: SVG 1.1 or SVG 2.0
- **Encoding**: UTF-8

### Supported Features
ThorVG supports most SVG features including:
- Basic shapes (rect, circle, ellipse, line, polyline, polygon)
- Paths and bezier curves
- Gradients (linear and radial)
- Opacity and transparency
- Transformations (translate, rotate, scale)
- Groups and layers
- Text (basic support)

### Not Supported
- Animations (use Lottie instead)
- Filters
- JavaScript/scripting
- External resources

## Performance Tips

### 1. Optimize SVG Files
```bash
# Use SVGO to optimize SVG files
npm install -g svgo
svgo input.svg -o output.svg
```

### 2. Pre-render Complex SVGs
For very complex SVG files, consider pre-rendering to PNG for better performance:
```bash
# Convert to optimized PNG if SVG is too complex
inkscape -w 128 -h 128 complex.svg -o simple.png
```

### 3. Use Appropriate Sizes
SVG rendering uses CPU/GPU resources. Size your SVG display to match typical usage:
```yaml
# Good: Reasonable size
- image:
    src: "icon.svg"
    width: 64
    height: 64

# Avoid: Unnecessarily large
- image:
    src: "icon.svg"
    width: 1024  # Too large, will use more resources
    height: 1024
```

## Example: Icon Set

### Directory Structure
```
/sdcard/
  icons/
    weather/
      sun.svg
      cloud.svg
      rain.svg
      snow.svg
    ui/
      home.svg
      settings.svg
      back.svg
      menu.svg
```

### Implementation
```yaml
lvgl:
  pages:
    - id: home_page
      widgets:
        # Weather icon (SVG)
        - image:
            id: weather_icon
            src: "/sdcard/icons/weather/sun.svg"
            x: 20
            y: 20
            width: 48
            height: 48

        # Settings button (SVG icon)
        - button:
            id: settings_btn
            x: 720
            y: 20
            width: 60
            height: 60
            widgets:
              - image:
                  src: "/sdcard/icons/ui/settings.svg"
                  align: CENTER
```

## Free SVG Icon Resources

### Icon Libraries
- **Remix Icon** - https://remixicon.com/ (2000+ icons, MIT license)
- **Heroicons** - https://heroicons.com/ (300+ icons, MIT license)
- **Lucide** - https://lucide.dev/ (1000+ icons, ISC license)
- **Tabler Icons** - https://tabler-icons.io/ (4000+ icons, MIT license)
- **Feather** - https://feathericons.com/ (280+ icons, MIT license)

### Weather Icons
- **Weather Icons** - https://erikflowers.github.io/weather-icons/ (222 icons)
- **Meteocons** - https://bas.dev/work/meteocons (100+ animated weather icons)

### How to Download
```bash
# Example: Download from Remix Icon
mkdir -p /sdcard/icons
cd /sdcard/icons

# Download individual SVG files
wget https://cdn.jsdelivr.net/npm/remixicon@3.5.0/icons/Weather/sun-line.svg -O sun.svg
wget https://cdn.jsdelivr.net/npm/remixicon@3.5.0/icons/Weather/cloud-line.svg -O cloud.svg
wget https://cdn.jsdelivr.net/npm/remixicon@3.5.0/icons/Weather/rainy-line.svg -O rain.svg
```

## Troubleshooting

### SVG Not Displaying
1. Check that ThorVG is enabled in build:
   ```
   [lvgl_advanced_features:xxx] ThorVG Internal: ENABLED
   [lvgl_advanced_features:xxx] SVG Support: ENABLED
   ```

2. Verify SVG file exists and is valid:
   ```bash
   # Check file exists
   ls -lh /sdcard/icons/sun.svg

   # Validate SVG
   xmllint --noout /sdcard/icons/sun.svg
   ```

3. Check LVGL logs for errors:
   ```yaml
   logger:
     logs:
       lvgl: DEBUG
   ```

### Poor Performance
- Reduce SVG complexity (fewer paths/nodes)
- Use smaller display sizes
- Pre-render complex SVGs to PNG
- Limit number of SVG images on screen simultaneously

### Memory Issues
- SVG rendering requires more RAM than bitmap images
- Monitor PSRAM usage:
  ```cpp
  ESP_LOGI("memory", "Free PSRAM: %d", esp_get_free_heap_size());
  ```
- Consider bitmap fallbacks for memory-constrained situations

## Comparison: SVG vs PNG vs Lottie

| Feature | SVG | PNG | Lottie |
|---------|-----|-----|--------|
| Scalable | ✅ Perfect | ❌ Pixelated | ✅ Perfect |
| Animated | ❌ Static | ❌ Static | ✅ Animated |
| File Size | 🟡 Small-Medium | 🔴 Large | 🟢 Very Small |
| CPU Usage | 🟡 Medium | 🟢 Low | 🟡 Medium |
| RAM Usage | 🟡 Medium | 🔴 High | 🟡 Medium |
| Color | ✅ Unlimited | ✅ Unlimited | ✅ Unlimited |

### Recommendations
- **Static Icons**: Use SVG
- **Photos/Complex Images**: Use PNG/JPEG
- **Animations**: Use Lottie
- **Simple Graphics**: Use SVG
- **Memory Constrained**: Use Lottie or optimized PNG

## Advanced: SVG Data from Memory

Instead of loading from file, you can embed SVG data in code:

```yaml
lvgl:
  on_...:
    then:
      - lambda: |-
          // Embedded SVG as string
          const char* svg_data = R"SVG(
          <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">
            <circle cx="12" cy="12" r="10" fill="#FFD700"/>
            <path d="M12 2 L15 10 L23 10 L17 15 L19 23 L12 18 L5 23 L7 15 L1 10 L9 10 Z" fill="#FFF"/>
          </svg>
          )SVG";

          // Load SVG from memory
          lv_image_set_src(id(my_icon), svg_data);
```

## Reference

- **LVGL SVG Documentation**: https://docs.lvgl.io/master/widgets/image.html
- **ThorVG Website**: https://www.thorvg.org/
- **SVG Specification**: https://www.w3.org/TR/SVG2/
