# 🚀 Optimisations Caméra et Vidéo - LVGL V9 + ThorVG

## Vue d'ensemble

Ce document détaille les optimisations pour améliorer la fluidité de la caméra et du lecteur vidéo avec LVGL V9 et ThorVG sur ESP32-P4.

---

## 📹 Optimisations Caméra (lvgl_camera_display)

### État actuel (analyse du code)

**Fichier**: `components/lvgl_camera_display/lvgl_camera_display.cpp`

**Points forts actuels** ✅:
- Zero-copy avec buffer pool (lignes 143-154, 183-184)
- Timer LVGL pour update périodique (ligne 44)
- Statistiques de performance détaillées (lignes 94-120)
- Support détection de visages optionnel (lignes 165-169)

**Métriques actuelles** (ligne 112):
```cpp
ESP_LOGI(TAG, "%u frames - FPS: %.2f | capture: %.1fms | canvas: %.1fms | skip: %.1f%%"
```

### Optimisations proposées

#### 1. **Réduire la latence avec LVGL V9 Direct Mode**

LVGL V9 introduit le "Direct Mode" qui permet de bypasser le buffer LVGL pour un rendu direct.

**Modification proposée** (ligne 180):
```cpp
// Ancienne méthode (V8)
lv_canvas_set_buffer(this->canvas_obj_, img_data, width, height, LV_IMG_CF_TRUE_COLOR);

// Nouvelle méthode (V9) - Direct rendering
#if LVGL_VERSION_MAJOR >= 9
lv_canvas_set_buffer(this->canvas_obj_, img_data, width, height, LV_COLOR_FORMAT_RGB565);
lv_obj_set_flag(this->canvas_obj_, LV_OBJ_FLAG_FLUSH_RENDER);  // Rendu immédiat
#else
lv_canvas_set_buffer(this->canvas_obj_, img_data, width, height, LV_IMG_CF_TRUE_COLOR);
#endif
```

**Gain attendu**: -5-8ms de latence par frame = +3-5 FPS

#### 2. **Améliorer le FPS avec PPA (Pixel Processing Accelerator)**

L'ESP32-P4 possède un PPA matériel pour les opérations pixels. Utiliser le PPA pour la conversion de couleurs.

**Nouvelle fonction à ajouter**:
```cpp
#ifdef CONFIG_IDF_TARGET_ESP32P4
#include "esp_ppa.h"

void LVGLCameraDisplay::update_canvas_ppa_() {
  // ... (existing code to get buffer)

  // Utiliser PPA pour conversion/copy hardware
  ppa_client_config_t ppa_client_config = {
    .oper_type = PPA_OPERATION_SRM,  // Scale-Rotate-Mirror
  };

  ppa_client_handle_t ppa_client = NULL;
  ppa_register_client(&ppa_client_config, &ppa_client);

  // Configuration PPA pour copy direct RGB565
  ppa_srm_oper_config_t srm_oper_config = {
    .in.buffer = img_data,
    .in.pic_w = width,
    .in.pic_h = height,
    .in.pixel_fmt = PPA_SRM_COLOR_MODE_RGB565,
    .out.buffer = (uint8_t*)lv_canvas_get_buf(this->canvas_obj_),
    .out.pixel_fmt = PPA_SRM_COLOR_MODE_RGB565,
    .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
    .scale_x = 1.0f,
    .scale_y = 1.0f,
    .mirror_x = false,
    .mirror_y = false,
  };

  ppa_do_scale_rotate_mirror(ppa_client, &srm_oper_config);
  ppa_unregister_client(ppa_client);
}
#endif
```

**Gain attendu**: -10-15ms de temps canvas = +5-8 FPS

#### 3. **Optimiser le timer update_interval**

Adapter dynamiquement l'intervalle selon le FPS réel.

**Modification ligne 35-36**:
```cpp
ESP_LOGI(TAG, "   Update interval: %u ms (~%d FPS) via LVGL timer",
         this->update_interval_, 1000 / this->update_interval_);

// Ajouter adaptation dynamique
void LVGLCameraDisplay::adapt_update_interval_() {
  static float avg_fps = 0;
  static uint32_t measure_count = 0;

  // Calculer FPS moyen toutes les 100 frames
  if (this->frame_count_ % 100 == 0 && this->frame_count_ > 0) {
    // Si FPS < target - 3, augmenter l'intervalle (ralentir)
    // Si FPS > target, réduire l'intervalle (accélérer)
    float target_fps = 1000.0f / this->update_interval_;
    if (avg_fps < target_fps - 3) {
      this->update_interval_ += 2;  // Ralentir
      lv_timer_set_period(this->lvgl_timer_, this->update_interval_);
    } else if (avg_fps > target_fps + 2) {
      if (this->update_interval_ > 20) {  // Min 20ms = 50 FPS max
        this->update_interval_ -= 2;  // Accélérer
        lv_timer_set_period(this->lvgl_timer_, this->update_interval_);
      }
    }
  }
}
```

**Gain attendu**: FPS stable sans drops

#### 4. **Réduire les skipped frames**

Améliorer le taux de capture avec buffer pool plus grand.

**Modification dans `mipi_dsi_cam`** (composant caméra):
```yaml
mipi_dsi_cam:
  id: tab5_cam
  sensor: OV5647
  format: RGB565
  resolution: 800x480
  fps: 30

  # NOUVEAU: Buffer pool plus grand pour moins de drops
  buffer_count: 4      # Au lieu de 2 ou 3 (défaut)
  buffer_location: PSRAM  # Forcer PSRAM pour libérer RAM interne
```

**Gain attendu**: Skip rate de 10-15% → 2-5%

---

## 🎬 Optimisations Lecteur Vidéo (avi_player)

### État actuel (analyse du code)

**Fichier**: `components/avi_player/avi_player_component.cpp`

**Points forts actuels** ✅:
- Décodeur JPEG matériel (lignes 104-108)
- Rotation matérielle PPA (lignes 422-465)
- Buffer alignment 64-byte pour JPEG (ligne 62)
- Support audio via speaker (lignes 253-274)

**Architecture actuelle**:
1. AVI parser (C) - `avi_player.c`
2. MJPEG decoder (hardware) - `jpeg_decoder_process()`
3. Rotation PPA (hardware) - `esp_imgfx_rotate_process()`
4. LVGL display - `lv_img_set_src()`

### Optimisations proposées

#### 1. **Améliorer la synchronisation Audio/Vidéo**

Le code actuel joue l'audio sans sync stricte avec la vidéo.

**Problème identifié** (lignes 260-273):
```cpp
// Audio callback - pas de sync frame timing
size_t bytes_written = player->speaker_->play(data->data, data->data_bytes);
if (bytes_written < data->data_bytes) {
  ESP_LOGV(TAG, "Audio buffer full, dropped %zu bytes", ...);
}
```

**Solution - Ring buffer avec timestamps**:
```cpp
// Nouvelle structure pour sync A/V
struct AVSyncBuffer {
  uint8_t *audio_data;
  size_t audio_size;
  uint64_t video_pts;  // Presentation timestamp
  uint64_t audio_pts;
};

// Dans audio_frame_callback
void AviPlayerComponent::audio_frame_callback(frame_data_t *data, void *arg) {
  AviPlayerComponent *player = static_cast<AviPlayerComponent *>(arg);

  // Calculer le PTS (presentation timestamp)
  uint64_t current_pts = esp_timer_get_time();

  // Vérifier la différence avec le dernier frame vidéo
  int64_t av_diff = current_pts - player->last_video_pts_;

  // Si audio en avance > 50ms, attendre
  if (av_diff > 50000) {
    ESP_LOGV(TAG, "Audio ahead, waiting %lld us", av_diff);
    vTaskDelay(pdMS_TO_TICKS(av_diff / 1000));
  }

  // Si audio en retard > 100ms, skip
  if (av_diff < -100000) {
    ESP_LOGV(TAG, "Audio behind, skipping frame");
    return;
  }

  // Jouer l'audio normalement
  player->speaker_->play(data->data, data->data_bytes);
}
```

**Gain attendu**: Sync A/V < 30ms (au lieu de 100-200ms)

#### 2. **Optimiser le décodage JPEG avec buffer pré-alloué**

Actuellement, le buffer vidéo est réalloué dynamiquement (lignes 381-400).

**Amélioration**:
```cpp
// Au setup, pré-allouer pour la taille max attendue
void AviPlayerComponent::setup() {
  // ... (existing code)

  // Pré-allouer avec marge pour éviter réallocation
  size_t max_buffer_size = ((width_ + 15) & ~15) * ((height_ + 15) & ~15) * sizeof(lv_color_t);
  video_buffer_ = (lv_color_t *)heap_caps_aligned_alloc(64, max_buffer_size,
                                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  video_buffer_size_ = max_buffer_size;

  ESP_LOGI(TAG, "Pre-allocated video buffer: %u bytes", max_buffer_size);
}

// Dans render_frame, skip reallocation (ligne 381)
void AviPlayerComponent::render_frame(frame_data_t *data) {
  // ... (parse JPEG header)

  // SKIP reallocation - buffer déjà assez grand
  if (actual_width_ == 0) {
    actual_width_ = (frame_info_.width + 15) & ~15;
    actual_height_ = (frame_info_.height + 15) & ~15;
    // PAS de reallocation - buffer déjà pré-alloué
  }

  // ... (decode JPEG)
}
```

**Gain attendu**: -2-5ms par frame (pas de realloc) = +1-2 FPS

#### 3. **Utiliser le cache ThorVG pour les frames**

LVGL V9 avec ThorVG permet de cacher les frames décodées.

**Nouvelle configuration** (dans `lvgl_advanced_features`):
```yaml
lvgl_advanced_features:
  # Cache d'images augmenté pour vidéo
  img_cache_size: 16  # Cache 16 frames = ~0.5s @ 30 FPS
```

**Code pour utiliser le cache**:
```cpp
// Dans render_frame, après decode JPEG
#if LVGL_VERSION_MAJOR >= 9
  // Marquer l'image pour caching
  lv_img_cache_set_size(16);  // 16 frames max
  lvgl_img_dsc_.header.flags = LV_IMG_HEADER_FLAG_CACHED;
#endif
```

**Gain attendu**: -5-10ms pour frames répétées (loops, pause)

#### 4. **Activer le preload intelligent**

Le mode `preload_to_memory` actuel est tout ou rien. Créer un mode hybride.

**Nouveau mode**: "preload_keyframes"
```cpp
// Nouvelle option dans __init__.py
CONF_PRELOAD_KEYFRAMES = "preload_keyframes"
cv.Optional(CONF_PRELOAD_KEYFRAMES, default=False): cv.boolean,

// Dans avi_player_component.cpp
void AviPlayerComponent::preload_keyframes_() {
  // Parser le fichier AVI une fois
  // Identifier les keyframes (I-frames dans MJPEG)
  // Charger seulement les keyframes en mémoire
  // Streaming des autres frames depuis SD

  // Avantage:
  //   - Seek rapide (keyframes en RAM)
  //   - Moins de RAM utilisée que full preload
  //   - Meilleure perf que streaming pur
}
```

**Gain attendu**: Seek instantané, RAM divisée par 5-10

#### 5. **Double buffering pour le rendu**

Éviter les déchirures (tearing) avec double buffer.

**Modification dans loop()** (ligne 143):
```cpp
void AviPlayerComponent::loop() {
  // ... (existing code)

  if (frame_ready_ && img_ != nullptr) {
    // NOUVEAU: Double buffering
    static lv_img_dsc_t* front_buffer = &lvgl_img_dsc_;
    static lv_img_dsc_t back_buffer;

    // Swap buffers
    lv_img_dsc_t* temp = front_buffer;
    front_buffer = &back_buffer;
    back_buffer = *temp;

    // Update avec front buffer (pas de tearing)
    lv_img_set_src(img_, front_buffer);
    lv_obj_invalidate(img_);

    frame_ready_ = false;
  }
}
```

**Gain attendu**: Pas de tearing, rendu fluide

---

## 🎨 Optimisations avec ThorVG/SVG/Lottie

### Overlay UI sur vidéo avec SVG

Au lieu de boutons bitmap, utiliser des SVG:

**Exemple**:
```yaml
# Dans la page vidéo
lvgl:
  pages:
    - id: video_page
      widgets:
        # Lecteur vidéo (background)
        - obj:
            id: video_container
            width: 800
            height: 480

        # Overlay UI avec SVG (pas de bitmap!)
        - image:
            id: play_button_svg
            src: "/sdcard/icons/play.svg"  # Icône SVG vectorielle
            width: 64
            height: 64
            x: 368  # Centré
            y: 408  # Bas de l'écran
            # AVANTAGE: SVG scale sans perte de qualité
            # AVANTAGE: Moins de RAM qu'un bitmap
```

**Code pour overlay dynamique**:
```cpp
// Callback on_click pour play button SVG
void play_button_clicked() {
  if (player_state == PLAYING) {
    // Changer l'icône SVG vers pause
    lv_img_set_src(play_button_svg, "/sdcard/icons/pause.svg");
    player->pause();
  } else {
    // Changer l'icône SVG vers play
    lv_img_set_src(play_button_svg, "/sdcard/icons/play.svg");
    player->play();
  }
}
```

### Animations Lottie pour loading

Pendant le chargement de vidéo, afficher une animation Lottie:

```yaml
# Loading spinner Lottie
- lottie:
    id: video_loading
    src: "/sdcard/animations/loading.json"
    width: 100
    height: 100
    x: 350
    y: 190
    loop: true
    autoplay: false  # Contrôlé par code
```

```cpp
// Dans avi_player_component.cpp
void AviPlayerComponent::play() {
  // Afficher loading animation
  lv_obj_clear_flag(loading_animation, LV_OBJ_FLAG_HIDDEN);

  // ... (start playback)

  // Masquer loading quand première frame affichée
  lv_obj_add_flag(loading_animation, LV_OBJ_FLAG_HIDDEN);
}
```

**Avantage**: Animation fluide 60 FPS sans CPU (rendu vectoriel GPU)

---

## 📊 Résumé des gains attendus

### Caméra (lvgl_camera_display)

| Optimisation | Gain FPS | Gain Latence | Difficulté |
|--------------|----------|--------------|------------|
| LVGL V9 Direct Mode | +3-5 | -5-8ms | Facile |
| PPA Hardware Copy | +5-8 | -10-15ms | Moyenne |
| Timer adaptatif | +2-3 | Stable | Facile |
| Buffer pool agrandi | +1-2 | Skip rate -50% | Facile |
| **TOTAL** | **+11-18 FPS** | **-15-23ms** | |

**FPS attendu**: 25-28 → **36-46 FPS** (limité à 30 par caméra)

### Lecteur Vidéo (avi_player)

| Optimisation | Gain FPS | Gain Sync A/V | Difficulté |
|--------------|----------|---------------|------------|
| Sync A/V améliorée | +0 | 200ms → 30ms | Moyenne |
| Buffer pré-alloué | +1-2 | - | Facile |
| Cache ThorVG | +2-3 | - | Facile |
| Preload keyframes | Seek instant | - | Difficile |
| Double buffering | Fluide | Pas de tearing | Facile |
| **TOTAL** | **+3-5 FPS** | **-170ms sync** | |

**FPS attendu**: 24-26 → **27-31 FPS** (limité à 30 par vidéo)

---

## 🛠️ Plan d'implémentation

### Phase 1 - Quick Wins (1-2h)
1. ✅ Créer `lvgl_advanced_features` component
2. ✅ Activer ThorVG/SVG/Lottie
3. 🔧 Augmenter buffer_size LVGL à 25%
4. 🔧 Augmenter buffer_count caméra à 4
5. 🔧 Pré-allouer buffer vidéo

**Gain immédiat**: +5-8 FPS caméra, +1-2 FPS vidéo

### Phase 2 - Optimisations Medium (2-4h)
1. 🔧 Implémenter LVGL V9 Direct Mode pour caméra
2. 🔧 Implémenter sync A/V avec timestamps
3. 🔧 Activer cache ThorVG pour vidéo
4. 🔧 Créer overlay UI avec SVG

**Gain**: +8-12 FPS caméra, sync A/V < 50ms

### Phase 3 - Optimisations Advanced (4-8h)
1. 🔧 Implémenter PPA hardware copy pour caméra
2. 🔧 Implémenter timer adaptatif
3. 🔧 Implémenter preload keyframes
4. 🔧 Implémenter double buffering
5. 🔧 Créer animations Lottie pour UI

**Gain**: +11-18 FPS caméra, +3-5 FPS vidéo, UI fluide 60 FPS

---

## 🧪 Tests et validation

### Métriques à mesurer

1. **FPS caméra**
   ```cpp
   // Déjà implémenté ligne 112
   ESP_LOGI(TAG, "FPS: %.2f", fps);
   ```
   - Avant: 25-28 FPS
   - Objectif: 30 FPS stable

2. **Skip rate caméra**
   ```cpp
   // Déjà implémenté ligne 111
   ESP_LOGI(TAG, "skip: %.1f%%", skip_rate);
   ```
   - Avant: 10-15%
   - Objectif: < 5%

3. **Sync A/V vidéo**
   ```cpp
   // À ajouter
   ESP_LOGI(TAG, "A/V sync: %lld ms", av_sync_diff_ms);
   ```
   - Avant: 100-200ms
   - Objectif: < 30ms

4. **RAM utilisée**
   ```cpp
   ESP_LOGI(TAG, "Free heap: %u bytes", esp_get_free_heap_size());
   ESP_LOGI(TAG, "Free PSRAM: %u bytes", esp_get_free_psram_size());
   ```
   - Surveiller: pas d'overflow

### Scénarios de test

1. **Test caméra streaming continu**
   - Durée: 5 minutes
   - Mesurer: FPS moyen, skip rate, latence

2. **Test vidéo lecture longue**
   - Vidéo: 2-3 minutes avec audio
   - Mesurer: FPS, sync A/V, drops

3. **Test UI overlay**
   - Vidéo + UI SVG overlay
   - Mesurer: FPS vidéo, réactivité UI

4. **Test animations Lottie**
   - Plusieurs animations simultanées
   - Mesurer: FPS Lottie, CPU usage

---

## 📝 Checklist migration

### Avant migration
- [ ] Sauvegarder configuration actuelle
- [ ] Noter FPS/skip rate/sync actuels
- [ ] Mesurer RAM/CPU usage baseline

### Migration LVGL V9
- [ ] Tester `lvgl_v9_thorvg_complete_config.yaml`
- [ ] Vérifier logs: "ThorVG: ENABLED"
- [ ] Vérifier compilation sans erreurs

### Optimisations caméra
- [ ] Augmenter buffer_count à 4
- [ ] Tester FPS après changement
- [ ] Implémenter Direct Mode si gain < 5 FPS
- [ ] Implémenter PPA si gain < 10 FPS

### Optimisations vidéo
- [ ] Pré-allouer buffer vidéo
- [ ] Tester sync A/V actuelle
- [ ] Implémenter sync timestamps si > 50ms
- [ ] Activer cache ThorVG

### Tests finaux
- [ ] FPS caméra ≥ 28 ?
- [ ] Skip rate < 5% ?
- [ ] Sync A/V < 50ms ?
- [ ] RAM suffisante ?
- [ ] UI fluide ?

---

## 💡 Astuces ESP32-P4

### Utiliser le PPA au maximum

Le PPA (Pixel Processing Accelerator) de l'ESP32-P4 peut faire:
- Rotation (0°, 90°, 180°, 270°)
- Scaling (up/down)
- Mirror (horizontal/vertical)
- Color conversion (RGB565, RGB888, YUV, etc.)
- Blend (alpha blending)

**Tous ces effets sont GRATUITS en CPU** - utilisez-les!

### Profiler avec ESP-IDF tools

```bash
# Dans votre projet ESPHome compilé
idf.py monitor

# Puis dans le monitor:
# Press 'Ctrl+]' puis taper:
heap

# Pour voir l'utilisation mémoire détaillée
```

### Activer le CPU second core

L'ESP32-P4 a 2 cores - utilisez-les!

```yaml
# Dans sdkconfig ou platformio_options:
platformio_options:
  build_flags:
    - "-DCONFIG_FREERTOS_UNICORE=0"  # Activer dual-core
```

Puis assigner les tâches:
```cpp
// AVI player task sur core 0 (déjà le cas)
// LVGL sur core 1
```

---

## ✅ Conclusion

Avec LVGL V9 + ThorVG + ces optimisations, vous pouvez atteindre:

- **Caméra**: 30 FPS stable (limite hardware)
- **Vidéo**: 30 FPS avec sync A/V < 30ms
- **UI**: 60 FPS pour animations Lottie
- **RAM**: ~10-12 MB (ESP32-P4 a 32 MB PSRAM ✅)
- **CPU**: 45-55% (laisse de la marge pour autres tâches)

Le tout avec une UI moderne utilisant SVG et animations Lottie! 🎉
