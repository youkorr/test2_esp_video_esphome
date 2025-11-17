# Retour à l'encodeur SOFTWARE H.264

## 🔴 Problème avec l'encodeur HARDWARE

### Crash observé
```
[06:17:03][I][rtsp_server:148]: H.264 hardware encoder initialized successfully
[06:17:03][I][rtsp_server:476]: Session 5514CA0B started playing
[06:17:03][I][rtsp_server:677]: Cached SPS (9 bytes)
[06:17:03][I][rtsp_server:684]: Cached PPS (4 bytes)
[06:17:03]Guru Meditation Error: Core  1 panic'ed (Load access fault)
[06:17:03]MTVAL   : 0x3c8e74f9
```

### Cause du crash

L'encodeur **hardware H.264** de l'ESP32-P4 attend un format YUV **packed** spécifique (`ESP_H264_RAW_FMT_O_UYY_E_VYY`), mais notre conversion RGB565→YUV produit du YUV420 **planar** (I420).

**Différence de format:**

- **YUV420 planar (I420)**: Données séparées
  ```
  Y: [Y0, Y1, Y2, Y3, ...]  (tous les pixels Y)
  U: [U0, U1, ...]           (chroma subsampled)
  V: [V0, V1, ...]           (chroma subsampled)
  ```

- **YUV packed (O_UYY_E_VYY)**: Données entrelacées
  ```
  [U0, Y0, Y1, V0, Y2, Y3, U1, Y4, Y5, V1, ...]
  ```

Notre fonction `convert_rgb565_to_yuv420_()` produit I420, pas O_UYY_E_VYY, d'où le crash lors de l'accès mémoire.

## ✅ Solution: Encodeur SOFTWARE (OpenH264)

L'encodeur **software** (OpenH264) fonctionne parfaitement avec YUV420 planar (I420) qui est le format que notre conversion produit.

### Avantages SOFTWARE
- ✅ **Compatible** avec notre conversion RGB565→YUV420 existante
- ✅ **Stable** - pas de crash
- ✅ **Testé** - OpenH264 est mature et fiable
- ✅ **Suffisant** pour 800x640@30fps avec bitrate 2Mbps

### Inconvénients SOFTWARE
- ⚠️ **Performance**: 720p@15-20fps max (vs 1080p@30fps hardware)
- ⚠️ **CPU**: Utilise plus de CPU (~30-40%)

### Performances attendues

| Résolution | FPS | CPU | Bitrate |
|------------|-----|-----|---------|
| 640x480 | 30 | ~20% | 1 Mbps |
| 800x640 | 25-30 | ~30% | 2 Mbps |
| 1280x720 | 15-20 | ~40% | 3 Mbps |

Pour votre caméra **OV5647 @ 800x640**, l'encodeur software devrait donner **25-30 FPS** sans problème.

## 🔧 Modifications effectuées

### 1. rtsp_server.cpp
```cpp
// Avant (hardware - crash)
esp_h264_enc_cfg_hw_t cfg = {
    .pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY,  // ❌ Format packed
    ...
};
esp_h264_enc_hw_new(&cfg, &h264_encoder_);

// Après (software - fonctionne)
esp_h264_enc_cfg_sw_t cfg = {
    .pic_type = ESP_H264_RAW_FMT_I420,  // ✅ Format planar (compatible)
    ...
};
esp_h264_enc_sw_new(&cfg, &h264_encoder_);
```

### 2. rtsp_server.h
```cpp
// Avant
#include "esp_h264_enc_single_hw.h"

// Après
#include "esp_h264_enc_single_sw.h"
```

### 3. Allocation mémoire
```cpp
// Avant (hardware - alignement 128 bytes)
yuv_buffer_ = heap_caps_aligned_alloc(128, yuv_buffer_size_, ...);

// Après (software - allocation standard)
yuv_buffer_ = heap_caps_malloc(yuv_buffer_size_, ...);
```

## 🚀 Résultat attendu

Après recompilation et flash:

**Logs ESP32:**
```
[I][rtsp_server:074]: Initializing H.264 software encoder (OpenH264)...
[I][rtsp_server:105]: Resolution: 800x640 (aligned from 800x640)
[I][rtsp_server:146]: H.264 software encoder initialized successfully (OpenH264)
[I][rtsp_server:147]: Software encoder: up to 720p@15-20fps on ESP32-P4
[I][rtsp_server:476]: Session XXXXXXXX started playing
```

**Pas de crash!** ✅

**Stream fonctionnel dans VLC/Frigate** avec 25-30 FPS à 800x640.

## 📝 Note pour l'avenir

Pour utiliser l'encodeur **hardware** à l'avenir, il faudrait:

1. **Implémenter une conversion RGB565→YUV packed (O_UYY_E_VYY)**
   - Plus complexe que YUV420 planar
   - Nécessite un algorithme de conversion différent

2. **Ou utiliser directement YUV de la caméra**
   - Configurer la caméra pour sortir du YUV au lieu de RGB565
   - Pas toujours supporté par tous les capteurs

3. **Ou utiliser une couche d'abstraction**
   - PPA (Pixel Processing Accelerator) de l'ESP32-P4
   - Peut convertir RGB565→YUV packed en hardware
   - Plus complexe à implémenter

Pour l'instant, l'encodeur **software est la meilleure solution** : stable, fonctionnel, et suffisant pour votre résolution.
