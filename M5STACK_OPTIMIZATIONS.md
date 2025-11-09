# Optimisations M5Stack Tab5 vs Notre Implémentation

Analyse du code M5Stack Tab5 hal_camera.cpp et comparaison avec notre mipi_dsi_cam

## ✅ Déjà implémenté (identique à M5Stack)

### 1. Zero-copy V4L2 MMAP
**M5Stack** :
```cpp
mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
```

**Notre code** : `mipi_dsi_cam.cpp:625-627`
```cpp
this->v4l2_buffers_[i].start = mmap(NULL, buf.length,
                                    PROT_READ | PROT_WRITE,
                                    MAP_SHARED, this->video_fd_, buf.m.offset);
```
✅ **Identique** - Zero-copy complet

---

### 2. RGB565 Direct (sans ISP)
**M5Stack** : Sortie RGB565 directe du capteur MIPI-CSI, pas de conversion RAW→RGB

**Notre code** : `pixel_format: RGB565` en zero-copy

✅ **Identique** - Pas d'overhead ISP

---

### 3. Rotating Buffers (2x)
**M5Stack** :
```cpp
#define EXAMPLE_VIDEO_BUFFER_COUNT (2)
VIDIOC_DQBUF → process → VIDIOC_QBUF
```

**Notre code** : `v4l2_buffers_[2]` avec cycle DQBUF/QBUF

✅ **Identique** - Double buffering standard

---

### 4. MIPI-CSI Configuration
**M5Stack** :
```cpp
esp_video_init_csi_config_t csi_config = {
    .csi_port = MIPI_CSI_PORT_0,
    .freq = 400000,
    .reset_pin = -1,
    .pwdn_pin = -1,
};
```

**Notre code** : Configuration identique dans esp_video_component.cpp

✅ **Identique**

---

## 🚀 Optimisations M5Stack à intégrer

### 1. ⭐⭐⭐⭐⭐ PPA (Pixel Processing Accelerator) Hardware

**Impact** : Économise 10-20% CPU pour mirror/rotate

**M5Stack utilise** :
```cpp
#include "esp_ppa.h"

ppa_client_handle_t ppa_client_handle;
ppa_client_config_t ppa_client_config = {
    .oper_type = PPA_OPERATION_SRM,  // Scale-Rotate-Mirror
};

ppa_client_srm_config_t srm_config = {
    .scale_x = 1.0f,
    .scale_y = 1.0f,
    .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
    .mirror_x = true,   // Hardware flip X
    .mirror_y = false,
};

ppa_do_scale_rotate_mirror(ppa_client_handle, &trans_desc, &event, portMAX_DELAY);
```

**Avantages** :
- Mirror/rotate sans CPU (DMA hardware)
- Latence <1ms
- Libère CPU pour imlib drawing

**Implémentation recommandée** :
- Ajouter `ppa_handle_` dans mipi_dsi_cam.h
- Initialiser PPA si mirror_x/mirror_y configurés
- Appliquer transformation après DQBUF, avant display

**Fichiers à modifier** :
- `mipi_dsi_cam.h` : Ajouter `void *ppa_handle_`
- `mipi_dsi_cam.cpp` : Ajouter `init_ppa_()` et `apply_ppa_transform_()`
- `__init__.py` : Ajouter options `mirror_x`, `mirror_y`, `rotation`

---

### 2. ⭐⭐⭐ Task Pinning (Core 1)

**Impact** : Réduit contention avec WiFi/BT (core 0)

**M5Stack** :
```cpp
xTaskCreatePinnedToCore(app_camera_display, "camera", 4096, NULL, 5, NULL, 1);
                                                                            ↑
                                                                         Core 1
```

**Avantages** :
- Core 0 : WiFi, Bluetooth, ESPHome main loop
- Core 1 : Caméra, LVGL, display → moins de context switching

**Implémentation** :
```yaml
# Dans YAML
mipi_dsi_cam:
  task_core: 1  # 0=core0 (défaut), 1=core1, -1=any core
```

```cpp
// Dans mipi_dsi_cam.cpp
#ifndef CONFIG_FREERTOS_UNICORE
  if (this->task_core_ >= 0 && this->task_core_ < portNUM_PROCESSORS) {
    xTaskCreatePinnedToCore(camera_task, "cam", 4096, this, 5,
                            &this->camera_task_handle_, this->task_core_);
  }
#endif
```

---

### 3. ⭐⭐ DMA-Capable SPIRAM

**Impact** : Améliore performance mémoire pour gros buffers

**M5Stack alloue** :
```cpp
heap_caps_calloc(img_show_size, 1, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
```

**Notre code** : V4L2 driver alloue automatiquement, mais on devrait vérifier

**Vérification à ajouter** :
```cpp
// Dans start_streaming(), après VIDIOC_QUERYCAP
ESP_LOGI(TAG, "V4L2 buffer capabilities:");
ESP_LOGI(TAG, "  - Streaming: %s", (cap.capabilities & V4L2_CAP_STREAMING) ? "YES" : "NO");
ESP_LOGI(TAG, "  - DMA: %s", (cap.device_caps & V4L2_CAP_EXT_PIX_FORMAT) ? "YES" : "NO");
```

**Pas urgent** : Les buffers V4L2 MMAP sont probablement déjà DMA-capable

---

### 4. ⭐ Mutex Thread-Safety

**Impact** : Évite race conditions si multithreading

**M5Stack** :
```cpp
std::mutex cam_mutex;

void capture() {
    std::lock_guard<std::mutex> lock(cam_mutex);
    // ... VIDIOC_DQBUF
}
```

**Notre code** : Pas de mutex (ESPHome single-threaded)

**À vérifier** : Si `lvgl_camera_display` appelle depuis thread LVGL séparé

**Implémentation si nécessaire** :
```cpp
// Dans mipi_dsi_cam.h
#include <mutex>
std::mutex frame_mutex_;

// Dans capture_frame()
bool MipiDSICamComponent::capture_frame() {
  std::lock_guard<std::mutex> lock(this->frame_mutex_);
  // ... existing code
}
```

---

### 5. ⭐ Minimal Task Delay

**Impact** : Réduit latence (négligeable si V4L2 bloque déjà)

**M5Stack** :
```cpp
vTaskDelay(pdMS_TO_TICKS(10));  // 100 FPS max
```

**Notre code** : `capture_frame()` bloque sur DQBUF jusqu'à frame disponible

**Action** : Pas nécessaire (VIDIOC_DQBUF est déjà bloquant)

---

## 📊 Comparaison Performance

| Feature | M5Stack Tab5 | Notre mipi_dsi_cam | Status |
|---------|-------------|-------------------|--------|
| Zero-copy MMAP | ✅ | ✅ | Identique |
| RGB565 direct | ✅ | ✅ | Identique |
| Rotating buffers (2x) | ✅ | ✅ | Identique |
| PPA hardware mirror/rotate | ✅ | ❌ | **À implémenter** |
| Task pinning core 1 | ✅ | ❌ | **À implémenter** |
| DMA-capable SPIRAM | ✅ (explicit) | ✅ (implicite) | Vérifier |
| Thread-safe mutex | ✅ | ❌ | Si multithreading |
| imlib drawing | ❌ | ✅ | **Notre avantage** |

---

## 🎯 Priorités d'implémentation

### Priorité 1 : PPA Hardware Transform (High Impact)
- Économise CPU significatif
- Latence minimale
- Utilisé par M5Stack pour mirror X

### Priorité 2 : Task Pinning (Medium Impact)
- Améliore stabilité WiFi + caméra simultanés
- Facile à implémenter

### Priorité 3 : Thread Mutex (Low Impact, si nécessaire)
- Seulement si lvgl_camera_display = multithreaded

---

## 📝 Code M5Stack analysé

Fichier : https://github.com/m5stack/M5Tab5-UserDemo/blob/main/platforms/tab5/main/hal/components/hal_camera.cpp

Points clés extraits :
```cpp
// 1. Initialisation CSI
esp_video_init_csi_config_t csi_config = {...};
esp_video_open("0", O_RDWR, &csi_config);

// 2. Format RGB565
esp_video_set_fmt(video, &fmt);  // V4L2_PIX_FMT_RGB565

// 3. MMAP buffers
v4l2_buffers[i].start = mmap(...);

// 4. PPA hardware transform
ppa_do_scale_rotate_mirror(ppa_client_handle, &trans, &event, portMAX_DELAY);

// 5. LVGL canvas display
lv_canvas_set_buffer(canvas, buffer, width, height, LV_COLOR_FORMAT_RGB565);
```

Notre implémentation est déjà **90% identique** à M5Stack. Les 10% restants = PPA hardware (priorité haute).
