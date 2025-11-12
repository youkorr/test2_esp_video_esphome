# Test des Formats Customs SC202CS et OV5647

## Objectif
Vérifier que les formats customs avec corrections d'exposition sont bien appliqués au runtime.

## Modifications Effectuées

### SC202CS (`sc202cs_custom_formats.h`)
```c
// Exposition settings (matching SC2336 working config)
{0x3e00, 0x00},  // Exposure high
{0x3e01, 0x4d},  // Exposure mid = 77 (from SC2336)
{0x3e02, 0xc0},  // Exposure low
{0x3e08, 0x1f},  // AEC/AGC enable (0x1f = enable auto exposure & auto gain)
{0x3e09, 0x00},  // Gain

static const esp_cam_sensor_isp_info_t sc202cs_vga_isp_info = {
    .isp_v1_info = {
        .exp_def = 0x4dc,     // 1244 - matching SC2336 working config
        .gain_def = 0,        // No default gain - matching SC2336
        .bayer_type = ESP_CAM_SENSOR_BAYER_BGGR,  // SC202CS is BGGR
    }
};
```

### OV5647 (`ov5647_custom_formats.h`)
```c
// AEC/AGC settings
{0x3503, 0x00},  // Enable auto exposure and auto gain (0x00 = both auto, 0x03 = both manual)

// VGA 640x480
static const esp_cam_sensor_isp_info_t ov5647_640x480_isp_info = {
    .isp_v1_info = {
        .exp_def = 0x300,     // 768 - restored to original value, let AEC handle it
        .gain_def = 0x100,
        .bayer_type = ESP_CAM_SENSOR_BAYER_BGGR,  // Changed from GBRG to fix red tint
    }
};
```

## Application des Formats Customs

Les formats customs sont appliqués dans `mipi_dsi_cam.cpp::start_streaming()`:

```cpp
// SC202CS @ VGA 640x480 (lignes 731-751)
if (this->sensor_name_ == "sc202cs") {
    if (width == 640 && height == 480) {
        custom_format = &sc202cs_format_vga_raw8_30fps;
        ESP_LOGI(TAG, "✅ Using CUSTOM format: VGA 640x480 RAW8 @ 30fps (SC202CS)");
    }

    if (custom_format != nullptr) {
        if (ioctl(this->video_fd_, VIDIOC_S_SENSOR_FMT, custom_format) != 0) {
            ESP_LOGE(TAG, "❌ VIDIOC_S_SENSOR_FMT failed: %s", strerror(errno));
        } else {
            ESP_LOGI(TAG, "✅ Custom format applied successfully!");
        }
    }
}

// OV5647 @ VGA 640x480 (lignes 703-725)
if (this->sensor_name_ == "ov5647") {
    if (width == 640 && height == 480) {
        custom_format = &ov5647_format_640x480_raw8_30fps;
        ESP_LOGI(TAG, "✅ Using CUSTOM format: VGA 640x480 RAW8 @ 30fps (OV5647)");
    }

    if (custom_format != nullptr) {
        if (ioctl(this->video_fd_, VIDIOC_S_SENSOR_FMT, custom_format) != 0) {
            ESP_LOGE(TAG, "❌ VIDIOC_S_SENSOR_FMT failed: %s", strerror(errno));
        } else {
            ESP_LOGI(TAG, "✅ Custom format applied successfully!");
        }
    }
}
```

## Logs à Vérifier

Lors du démarrage de la caméra, vous devez voir dans les logs série:

### Pour SC202CS:
```
[I][mipi_dsi_cam:737] ✅ Using CUSTOM format: VGA 640x480 RAW8 @ 30fps (SC202CS)
[I][mipi_dsi_cam:746] ✅ Custom format applied successfully!
[I][mipi_dsi_cam:747]    Sensor registers configured for native VGA (640x480)
```

### Pour OV5647:
```
[I][mipi_dsi_cam:709] ✅ Using CUSTOM format: VGA 640x480 RAW8 @ 30fps (OV5647)
[I][mipi_dsi_cam:721] ✅ Custom format applied successfully!
[I][mipi_dsi_cam:722]    Sensor registers configured for native 640x480
```

## Que Faire Si Les Logs Montrent Des Erreurs?

### Erreur: `❌ VIDIOC_S_SENSOR_FMT failed`
Cela signifie que le driver de capteur ne supporte pas les formats customs.
**Solution**: Vérifier que esp-idf est bien à jour et que le driver esp_cam_sensor supporte VIDIOC_S_SENSOR_FMT.

### Pas de log "Using CUSTOM format"
Le capteur ou la résolution ne correspond pas:
- Vérifier dans la configuration YAML: `sensor_type` et `resolution`
- SC202CS doit avoir `resolution: "VGA"` (640x480)
- OV5647 doit avoir `resolution: "VGA"` (640x480) ou `"1024x600"`

## Configuration YAML Requise

### Pour SC202CS:
```yaml
mipi_dsi_cam:
  id: my_cam
  i2c_id: bsp_bus
  sensor_type: sc202cs      # ← Important!
  sensor_addr: 0x36
  resolution: "VGA"         # ← Important! Déclenche custom format
  pixel_format: RGB565
  framerate: 30

lvgl_camera_display:
  id: camera_display
  camera_id: my_cam
  canvas_id: camera_canvas
  update_interval: 100ms
```

### Pour OV5647:
```yaml
mipi_dsi_cam:
  id: my_cam
  i2c_id: bsp_bus
  sensor_type: ov5647       # ← Important!
  sensor_addr: 0x36
  resolution: "VGA"         # ← Important! Déclenche custom format
  pixel_format: RGB565
  framerate: 30

lvgl_camera_display:
  id: camera_display
  camera_id: my_cam
  canvas_id: camera_canvas
  update_interval: 100ms
```

## Procédure de Test

1. **Compiler le firmware avec les corrections:**
   ```bash
   pio run -e esp32-p4-function-ev-board
   ```

2. **Flasher le firmware:**
   ```bash
   pio run -e esp32-p4-function-ev-board -t upload
   ```

3. **Surveiller les logs série:**
   ```bash
   pio device monitor -e esp32-p4-function-ev-board
   ```

4. **Chercher dans les logs:**
   - `✅ Using CUSTOM format: VGA 640x480 RAW8`
   - `✅ Custom format applied successfully!`
   - `✅ Sensor registers configured for native`

5. **Vérifier l'image sur l'écran LVGL:**
   - SC202CS: L'image devrait être moins lumineuse (exposition réduite)
   - OV5647: La teinte rouge devrait être corrigée (Bayer BGGR)

## Problèmes Connus

### 1. Format Custom Non Appliqué
**Symptôme**: Logs montrent "Custom format not supported, falling back to standard format"
**Cause**: Le driver esp_cam_sensor ne supporte pas VIDIOC_S_SENSOR_FMT
**Solution**: Mettre à jour esp-idf ou utiliser une approche différente (V4L2 controls)

### 2. Exposition Toujours Trop Élevée
**Symptôme**: Image toujours trop lumineuse malgré custom format
**Cause**: AEC/AGC hardware n'est pas activé ou ISP override les paramètres
**Solution**:
- Vérifier que register 0x3e08 = 0x1f est bien dans la config SC202CS
- Vérifier que register 0x3503 = 0x00 est bien dans la config OV5647
- Tester en désactivant IPA JSON (CONFIG_CAMERA_*_DEFAULT_IPA_JSON_CONFIGURATION_FILE=0)

### 3. Teinte Rouge Persiste sur OV5647
**Symptôme**: Image toujours rouge/rosée
**Cause**: Bayer pattern incorrect
**Solution**: Vérifier que `bayer_type = ESP_CAM_SENSOR_BAYER_BGGR` dans custom format

## Références

- **Waveshare SC2336 Configuration**: https://github.com/waveshareteam/esp-video/blob/main/exemples/video_custom_format/main/app_sc2336_custom_settings.h
- **ESP-IDF esp_cam_sensor**: https://github.com/espressif/esp-video/tree/main/components/esp_cam_sensor
- **Custom Formats Implementation**: `components/mipi_dsi_cam/mipi_dsi_cam.cpp` lignes 673-752
