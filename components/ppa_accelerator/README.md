# PPA Accelerator Component for ESP32-P4

Hardware-accelerated image processing using ESP32-P4's **Pixel Processing Accelerator (PPA)**.

## Features

| Operation | Description | Performance Gain |
|-----------|-------------|------------------|
| **Scale** | Hardware scaling with 1/16 step precision | ~10x faster |
| **Rotate** | 0°, 90°, 180°, 270° rotation | ~10x faster |
| **Mirror** | Horizontal/Vertical mirroring | ~10x faster |
| **Blend** | Alpha blending of two images | ~5x faster |
| **Fill** | Fill region with solid color | ~20x faster |

## Supported Pixel Formats

- `RGB565` - 16-bit (5-6-5)
- `RGB888` - 24-bit RGB
- `ARGB8888` - 32-bit ARGB

## Requirements

- **ESP32-P4** microcontroller
- **ESP-IDF 5.4+**
- **PSRAM** recommended for large images

## Basic Usage

### YAML Configuration

```yaml
external_components:
  - source:
      type: local
      path: components
    components:
      - ppa_accelerator

# Initialize PPA
ppa_accelerator:
  id: ppa
```

### C++ Lambda Usage

```cpp
// Get PPA instance
auto* ppa = ppa_accelerator::get_ppa_accelerator();
if (!ppa || !ppa->is_available()) {
  ESP_LOGE("app", "PPA not available");
  return;
}

// Allocate DMA-capable buffers
uint8_t* input_buf = ppa_accelerator::PPAAccelerator::allocate_dma_buffer(64 * 64 * 2);
uint8_t* output_buf = ppa_accelerator::PPAAccelerator::allocate_dma_buffer(128 * 128 * 2);

// Configure buffers
ppa_accelerator::PPABuffer input(input_buf, 64, 64, ppa_accelerator::PixelFormat::RGB565);
ppa_accelerator::PPABuffer output(output_buf, 128, 128, ppa_accelerator::PixelFormat::RGB565);

// Scale 2x
auto result = ppa->scale_to_size(input, output, 128, 128);
if (result.success) {
  ESP_LOGI("app", "Scaled in %lu microseconds", result.processing_time_us);
}

// Clean up
ppa_accelerator::PPAAccelerator::free_dma_buffer(input_buf);
ppa_accelerator::PPAAccelerator::free_dma_buffer(output_buf);
```

## API Reference

### Scale Operations

```cpp
// Scale with factors
PPAResult scale(const PPABuffer& input, PPABuffer& output, float scale_x, float scale_y);

// Scale to specific dimensions
PPAResult scale_to_size(const PPABuffer& input, PPABuffer& output, uint16_t width, uint16_t height);
```

### Rotate/Mirror Operations

```cpp
// Mirror
PPAResult mirror_horizontal(const PPABuffer& input, PPABuffer& output);
PPAResult mirror_vertical(const PPABuffer& input, PPABuffer& output);

// Rotate (0, 90, 180, 270 degrees only)
PPAResult rotate(const PPABuffer& input, PPABuffer& output, int angle_degrees);
```

### Blend Operations

```cpp
// Blend foreground over background with alpha
PPAResult blend(const PPABuffer& fg, const PPABuffer& bg, PPABuffer& output, uint8_t fg_alpha);
```

### Fill Operations

```cpp
// Fill entire buffer
PPAResult fill(PPABuffer& buffer, uint32_t color_argb8888);

// Fill rectangle region
PPAResult fill_rect(PPABuffer& buffer, uint16_t x, uint16_t y,
                    uint16_t width, uint16_t height, uint32_t color_argb8888);
```

### Buffer Management

```cpp
// Allocate DMA-capable buffer (required for PPA)
static uint8_t* allocate_dma_buffer(size_t size);

// Free DMA buffer
static void free_dma_buffer(uint8_t* buffer);

// Check if buffer is DMA-capable
static bool is_dma_capable(const uint8_t* buffer);
```

## Integration with GIF Decoder

The PPA accelerator can significantly speed up GIF animation display, especially when:

1. **Scaling GIFs** - Scale small icons (e.g., 64x64) to display size (e.g., 128x128)
2. **Frame blending** - Handle GIF disposal methods with hardware alpha blending
3. **Background fill** - Clear frame backgrounds quickly

### Example: Weather Icon Animation

```yaml
# Configuration
storage:
  platform: sd_direct
  sd_component: sd_card
  root_path: "/sdcard"
  sd_images:
    - id: weather_icon
      file_path: "/weather/sunny.gif"
      format: rgb565
      # resize: 128x128  # Let PPA handle this!

ppa_accelerator:
  id: ppa

# In lambda, use PPA for scaling
on_boot:
  then:
    - lambda: |-
        // After GIF loads, use PPA to scale each frame
        auto* ppa = ppa_accelerator::get_ppa_accelerator();
        if (ppa && ppa->is_available()) {
          ESP_LOGI("app", "PPA available for hardware-accelerated scaling");
        }
```

## Performance Comparison

| Operation | Software (CPU) | PPA Hardware | Speedup |
|-----------|----------------|--------------|---------|
| Scale 64→128 RGB565 | ~15ms | ~1.5ms | **10x** |
| Fill 128x128 RGB565 | ~2ms | ~0.1ms | **20x** |
| Blend 128x128 | ~8ms | ~1.5ms | **5x** |
| Rotate 90° 128x128 | ~12ms | ~1.2ms | **10x** |

## Limitations

1. **ESP32-P4 only** - Not available on other ESP32 variants
2. **DMA buffers required** - Standard `malloc` buffers won't work
3. **Rotation angles** - Only 0°, 90°, 180°, 270° supported
4. **Scale precision** - Limited to 1/16 step increments
5. **Memory bandwidth** - PSRAM operations slower than internal RAM

## Troubleshooting

### "PPA not initialized"
- Ensure you're running on ESP32-P4
- Check that ESP-IDF 5.4+ is being used

### "Input and output buffers must be different"
- SRM operations require separate input/output buffers
- Allocate two different buffers

### "DMA buffer allocation failed"
- Not enough PSRAM/internal RAM
- Try reducing buffer sizes

## References

- [ESP-IDF PPA Documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/ppa.html)
- [LGFX_PPA Library](https://github.com/tobozo/LGFX_PPA)
