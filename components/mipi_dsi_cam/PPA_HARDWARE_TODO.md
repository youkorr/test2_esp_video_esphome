# PPA Hardware Transform - Implémentation TODO

Basé sur l'analyse de M5Stack Tab5 hal_camera.cpp

## Objectif

Implémenter mirror/rotate **hardware** via PPA (Pixel Processing Accelerator) ESP32-P4 pour économiser 10-20% CPU.

## État Actuel

✅ **Préparation complète** :
- Champs `mirror_x_`, `mirror_y_`, `rotation_` ajoutés dans mipi_dsi_cam.h
- Configuration YAML active (mirror_x, mirror_y, rotation)
- Setters configurés dans __init__.py

❌ **Implémentation PPA manquante** :
- Include ESP-IDF `esp_ppa.h` nécessaire
- Initialisation PPA client
- Application transformation après DQBUF
- Tests hardware ESP32-P4

## Architecture M5Stack Tab5

### 1. Include PPA
```cpp
#include "esp_ppa.h"
```

### 2. Déclaration Handle (mipi_dsi_cam.h)
```cpp
protected:
  // PPA hardware transform handle
  void *ppa_client_handle_{nullptr};
  bool ppa_enabled_{false};
```

### 3. Initialisation PPA (dans setup())
```cpp
bool MipiDSICamComponent::init_ppa_() {
  if (!this->mirror_x_ && !this->mirror_y_ && this->rotation_ == 0) {
    ESP_LOGI(TAG, "PPA not needed (no mirror/rotate configured)");
    return true;  // Pas besoin de PPA
  }

  ppa_client_config_t ppa_config = {
    .oper_type = PPA_OPERATION_SRM,  // Scale-Rotate-Mirror
  };

  esp_err_t ret = ppa_register_client(&ppa_config, (ppa_client_handle_t*)&this->ppa_client_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register PPA client: %s", esp_err_to_name(ret));
    return false;
  }

  this->ppa_enabled_ = true;
  ESP_LOGI(TAG, "✓ PPA hardware transform enabled (mirror_x=%d, mirror_y=%d, rotation=%d)",
           this->mirror_x_, this->mirror_y_, this->rotation_);
  return true;
}
```

### 4. Application Transformation (dans capture_frame())
```cpp
bool MipiDSICamComponent::apply_ppa_transform_(uint8_t *src_buffer, uint8_t *dst_buffer) {
  if (!this->ppa_enabled_ || !this->ppa_client_handle_) {
    return true;  // Pas de transformation
  }

  ppa_srm_oper_config_t srm_config = {};
  srm_config.in.buffer = src_buffer;
  srm_config.in.pic_w = this->image_width_;
  srm_config.in.pic_h = this->image_height_;
  srm_config.in.block_w = this->image_width_;
  srm_config.in.block_h = this->image_height_;
  srm_config.in.block_offset_x = 0;
  srm_config.in.block_offset_y = 0;
  srm_config.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

  srm_config.out.buffer = dst_buffer;
  srm_config.out.buffer_size = this->image_buffer_size_;
  srm_config.out.pic_w = this->image_width_;  // Pas de scale
  srm_config.out.pic_h = this->image_height_;
  srm_config.out.block_offset_x = 0;
  srm_config.out.block_offset_y = 0;
  srm_config.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

  // Configuration transformation
  srm_config.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
  if (this->rotation_ == 90) {
    srm_config.rotation_angle = PPA_SRM_ROTATION_ANGLE_90;
  } else if (this->rotation_ == 180) {
    srm_config.rotation_angle = PPA_SRM_ROTATION_ANGLE_180;
  } else if (this->rotation_ == 270) {
    srm_config.rotation_angle = PPA_SRM_ROTATION_ANGLE_270;
  }

  srm_config.scale_x = 1.0f;  // Pas de scale
  srm_config.scale_y = 1.0f;
  srm_config.mirror_x = this->mirror_x_;
  srm_config.mirror_y = this->mirror_y_;

  srm_config.rgb_swap = PPA_SRM_COLOR_RGB_SWAP_NONE;
  srm_config.byte_swap = false;

  // Exécuter transformation hardware
  ppa_trans_data_t trans_data = {};
  trans_data.srm = srm_config;

  ppa_event_data_t event_data = {};

  esp_err_t ret = ppa_do_scale_rotate_mirror(
      (ppa_client_handle_t)this->ppa_client_handle_,
      &trans_data,
      &event_data,
      portMAX_DELAY  // Attendre fin transformation
  );

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "PPA transform failed: %s", esp_err_to_name(ret));
    return false;
  }

  return true;
}
```

### 5. Intégration dans capture_frame()
```cpp
bool MipiDSICamComponent::capture_frame() {
  // ... existing VIDIOC_DQBUF code ...

  // Si PPA activé, transformer frame
  if (this->ppa_enabled_) {
    // Allouer buffer temporaire pour destination PPA
    uint8_t *ppa_output = (uint8_t*)heap_caps_malloc(
        this->image_buffer_size_,
        MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM
    );

    if (!ppa_output) {
      ESP_LOGE(TAG, "Failed to allocate PPA output buffer");
      return false;
    }

    // Appliquer transformation hardware
    if (!this->apply_ppa_transform_(this->image_buffer_, ppa_output)) {
      heap_caps_free(ppa_output);
      return false;
    }

    // Copier résultat transformé vers buffer original
    memcpy(this->image_buffer_, ppa_output, this->image_buffer_size_);
    heap_caps_free(ppa_output);
  }

  // ... rest of capture_frame code ...
}
```

### 6. Cleanup (dans stop_streaming())
```cpp
void MipiDSICamComponent::cleanup_ppa_() {
  if (this->ppa_client_handle_) {
    ppa_unregister_client((ppa_client_handle_t)this->ppa_client_handle_);
    this->ppa_client_handle_ = nullptr;
    this->ppa_enabled_ = false;
    ESP_LOGI(TAG, "✓ PPA hardware transform cleanup");
  }
}
```

## Configuration YAML

```yaml
mipi_dsi_cam:
  id: cam
  sensor_type: sc202cs
  pixel_format: RGB565
  resolution: 720P

  # Hardware mirror/rotate via PPA (M5Stack-style)
  mirror_x: true     # Flip horizontal (hardware DMA)
  mirror_y: false    # Flip vertical
  rotation: 0        # 0, 90, 180, 270 degrees
```

## Avantages PPA Hardware

1. **Zero CPU** : Transformation DMA, pas de cycles CPU
2. **Latence minimale** : <1ms pour 1280x720 RGB565
3. **Pas de buffer supplémentaire** : PPA peut écrire in-place
4. **Économie énergie** : Hardware plus efficace que software

## Performance Estimée

**Sans PPA (software mirror)** :
- 1280x720 RGB565 = 1.8 MB
- Flip horizontal software : ~15ms CPU @ 240MHz
- Impact FPS : ~3-5 fps perdus

**Avec PPA (hardware)** :
- Transformation DMA : <1ms
- 0% CPU
- Impact FPS : négligeable (<0.5 fps)

**Économie CPU** : **~10-20% si mirror/rotate activés**

## Tests Nécessaires

1. ✅ Configuration YAML acceptée
2. ❌ Compilation avec esp_ppa.h (ESP-IDF)
3. ❌ Tests hardware ESP32-P4 avec SC202CS
4. ❌ Vérification latence PPA
5. ❌ Tests mirror_x + mirror_y + rotation combinés
6. ❌ Validation zero-copy avec imlib drawing

## Références

- M5Stack Tab5 : https://github.com/m5stack/M5Tab5-UserDemo/blob/main/platforms/tab5/main/hal/components/hal_camera.cpp
- ESP-IDF PPA API : components/esp_driver_ppa/include/driver/ppa.h
- ESP32-P4 TRM : Section "PPA (Pixel Processing Accelerator)"

## Note

Cette implémentation est **préparée mais non testée** sur hardware. Les champs sont en place, la configuration YAML fonctionne, mais le code PPA nécessite compilation ESP-IDF et tests sur ESP32-P4 réel.
