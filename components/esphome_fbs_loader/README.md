# ESPHome FBS Loader

**ESPHome component for loading ESP-DL FlatBuffers models**

This component provides an ESPHome-friendly interface to load and manage ESP-DL FlatBuffers models (.espdl files) from various storage locations.

## Features

- ✅ Load models from multiple storage locations:
  - FLASH RODATA (embedded in firmware)
  - FLASH Partition (SPIFFS/FAT)
  - SD Card
- ✅ Support for encrypted models (AES-128)
- ✅ Multi-model file support
- ✅ Access to model metadata and parameters
- ✅ Memory-efficient loading options
- ✅ Full integration with ESPHome's component system

## Installation

1. Copy the `esphome_fbs_loader` directory to your ESPHome project's `components/` folder
2. Ensure `esp-dl` component is available in `components/esp-dl/`
3. Add the component to your YAML configuration

## Basic Usage

```yaml
esphome_fbs_loader:
  - id: my_model_loader
    model_path: "model_data"          # Partition label or file path
    model_location: flash_partition   # Storage location
    model_name: "my_model.espdl"      # Optional: specific model name
    param_copy: true                  # Copy parameters to PSRAM
```

## Configuration Variables

### Required

- **id** (*Required*, ID): The ID for this component instance
- **model_location** (*Required*, enum): Where the model is stored
  - `flash_rodata`: Model embedded in firmware
  - `flash_partition`: Model in FLASH partition (default)
  - `sdcard`: Model on SD card

### Optional

- **model_path** (*Optional*, string): Path or partition label
  - For `flash_partition`: partition label (e.g., "model_data")
  - For `sdcard`: full file path (e.g., "/sdcard/models/model.espdl")
  - For `flash_rodata`: not used

- **model_name** (*Optional*, string): Name of model to load (for multi-model files)

- **model_index** (*Optional*, int): Index of model to load (0-based, for multi-model files)

- **encryption_key** (*Optional*, list of 16 bytes): AES-128 encryption key
  ```yaml
  encryption_key: [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                   0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f]
  ```

- **param_copy** (*Optional*, boolean): Copy model parameters to PSRAM. Default: `true`
  - `true`: Better performance, uses more PSRAM
  - `false`: Saves PSRAM, slower inference

## Examples

### Example 1: Load Model from FLASH Partition

```yaml
esphome_fbs_loader:
  - id: face_detector_loader
    model_path: "ai_models"
    model_location: flash_partition
    model_name: "human_face_detect_msr_s8_v1.espdl"
    param_copy: true
```

**Partition Table** (`partitions.csv`):
```
# Name,      Type, SubType, Offset,   Size,     Flags
nvs,         data, nvs,     0x9000,   0x5000,
otadata,     data, ota,     0xe000,   0x2000,
app0,        app,  ota_0,   0x10000,  0x300000,
app1,        app,  ota_1,   0x310000, 0x300000,
ai_models,   data, spiffs,  0x610000, 0x9F0000,
```

### Example 2: Load Encrypted Model

```yaml
esphome_fbs_loader:
  - id: encrypted_loader
    model_path: "secure_models"
    model_location: flash_partition
    encryption_key: [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                     0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f]
    param_copy: true
```

### Example 3: Load from SD Card

```yaml
esphome_fbs_loader:
  - id: sdcard_loader
    model_path: "/sdcard/ai_models/detector.espdl"
    model_location: sdcard
    param_copy: true
```

### Example 4: Load by Index (Multi-Model File)

```yaml
esphome_fbs_loader:
  - id: multi_model_loader
    model_path: "packed_models"
    model_location: flash_partition
    model_index: 0        # Load first model
    param_copy: true
```

### Example 5: Low PSRAM Mode

```yaml
esphome_fbs_loader:
  - id: low_memory_loader
    model_path: "model_data"
    model_location: flash_partition
    param_copy: false     # Don't copy to PSRAM (saves memory)
```

## Using Loaded Models in Code

### Access Model in Lambda

```yaml
lambda: |-
  auto loader = id(my_model_loader);
  if (loader.is_model_loaded()) {
    auto model = loader.get_model();

    // Get model information
    std::string name = model->get_model_name();
    int64_t version = model->get_model_version();

    // Get graph inputs/outputs
    auto inputs = model->get_graph_inputs();
    auto outputs = model->get_graph_outputs();

    // Get input shape
    std::vector<int> input_shape = model->get_value_info_shape(inputs[0]);

    ESP_LOGI("ai", "Model: %s v%lld", name.c_str(), version);
    ESP_LOGI("ai", "Input shape: [%d, %d, %d, %d]",
             input_shape[0], input_shape[1], input_shape[2], input_shape[3]);
  }
```

### Get Model Info Sensor

```yaml
text_sensor:
  - platform: template
    name: "AI Model Info"
    lambda: |-
      return id(my_model_loader).get_model_info();
```

### Model Loaded Binary Sensor

```yaml
binary_sensor:
  - platform: template
    name: "AI Model Status"
    lambda: |-
      return id(my_model_loader).is_model_loaded();
```

## C++ API Reference

### Methods

#### `bool is_model_loaded()`
Returns `true` if a model is successfully loaded.

#### `fbs::FbsModel* get_model()`
Returns pointer to the loaded FbsModel, or `nullptr` if not loaded.

#### `fbs::FbsLoader* get_loader()`
Returns pointer to the FbsLoader instance.

#### `std::string get_model_info()`
Returns a string with model name, version, and description.

#### `int get_model_count()`
Returns the number of models in the loaded file.

#### `void list_all_models()`
Logs all available models to console.

#### `void get_model_size(size_t *internal, size_t *psram, size_t *psram_rodata, size_t *flash)`
Gets memory usage information for the loaded model.

## Model Preparation

### Converting ONNX to ESP-DL Format

Use the `pack_espdl_models.py` script from esp-dl:

```bash
python components/esp-dl/fbs_loader/pack_espdl_models.py \
    --model_path model.onnx \
    --output_path model.espdl \
    --model_name my_model
```

### Encrypting Models

```bash
# Encrypt with AES-128
python components/esp-dl/fbs_loader/pack_espdl_models.py \
    --model_path model.onnx \
    --output_path model_encrypted.espdl \
    --model_name my_model \
    --encrypt \
    --key 000102030405060708090a0b0c0d0e0f
```

### Flashing Models to Partition

```bash
# Flash model to partition
esptool.py --chip esp32p4 \
    --port /dev/ttyUSB0 \
    write_flash \
    0x610000 model.espdl
```

Or use ESPHome build system:
```yaml
esphome:
  platformio_options:
    board_build.partitions: partitions.csv
    board_upload.flash_size: 32MB
```

## Memory Considerations

### PSRAM Usage

- **param_copy = true** (default)
  - ✅ Faster inference (PSRAM is faster than FLASH)
  - ❌ Uses more PSRAM
  - Use when: PSRAM is available and performance is critical

- **param_copy = false**
  - ✅ Saves PSRAM
  - ❌ Slower inference (reads from FLASH)
  - Use when: PSRAM is limited

### Model Size Limits

- **FLASH RODATA**: Limited by firmware size (~2-3 MB typical)
- **FLASH Partition**: Limited by partition size (can be several MB)
- **SD Card**: Limited by SD card capacity (GB range)

## Troubleshooting

### Model Not Loading

1. Check partition table configuration
2. Verify model file is flashed to correct partition
3. Check model format is correct (.espdl)
4. Verify encryption key if using encrypted model
5. Check logs for specific error messages

### Out of Memory Errors

1. Set `param_copy: false` to reduce PSRAM usage
2. Use smaller model
3. Increase PSRAM allocation in platformio_options
4. Use FLASH partition instead of RODATA

### Performance Issues

1. Set `param_copy: true` for better performance
2. Use FLASH partition instead of SD card
3. Optimize model size and complexity
4. Check PSRAM/FLASH clock settings

## Integration Examples

### With Human Face Detection

```yaml
esphome_fbs_loader:
  - id: face_model_loader
    model_path: "ai_models"
    model_location: flash_partition
    model_name: "human_face_detect_msr_s8_v1.espdl"

# Use in custom component
lambda: |-
  auto model = id(face_model_loader).get_model();
  if (model != nullptr) {
    // Perform face detection
    // ... your inference code
  }
```

## License

This component is part of the ESP32-P4 ESPHome video project.

## Credits

- Based on ESP-DL FlatBuffers loader
- ESP-DL: https://github.com/espressif/esp-dl
- ESPHome: https://esphome.io

## Support

For issues and questions:
- GitHub Issues: https://github.com/youkorr/test2_esp_video_esphome/issues
- ESPHome Discord: https://discord.gg/KhAMKrd
