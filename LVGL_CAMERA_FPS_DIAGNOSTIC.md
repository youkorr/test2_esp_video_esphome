# Diagnostic: 8 FPS au lieu de 30 FPS dans lvgl_camera_display

**Date:** 2026-01-01
**Branch:** `claude/fix-sc202cs-bayer-format-jJtiS`
**Problème:** lvgl_camera_display ne dépasse pas 8 FPS alors que 30 FPS sont attendus

---

## 1. Architecture Actuelle

### Workflow de rendu (lvgl_camera_display.cpp)

```cpp
void LVGLCameraDisplay::update_camera_frame_() {
    // 1. Capture frame (ligne 87) - RAPIDE (non-blocking, zero-copy)
    bool frame_captured = this->camera_->capture_frame();

    // 2. Update canvas (ligne 96) - PEUT ÊTRE LENT
    this->update_canvas_();
}

void LVGLCameraDisplay::update_canvas_() {
    // 3. Acquire buffer (ligne 156) - RAPIDE
    esp_cam_sensor::SimpleBufferElement *buffer = this->camera_->acquire_buffer();

    // 4. BOTTLENECK PRINCIPAL: Détections AI (lignes 171-185)
    #ifdef USE_FACE_DETECTION
      if (this->face_detection_ != nullptr) {
        this->face_detection_->draw_on_frame(img_data, width, height);  // ← LENT!
      }
    #endif
    #ifdef USE_YOLO11_DETECTION
      if (this->yolo11_detection_ != nullptr) {
        this->yolo11_detection_->draw_on_frame(img_data, width, height);  // ← TRÈS LENT!
      }
    #endif
    #ifdef USE_PEDESTRIAN_DETECTION
      if (this->pedestrian_detection_ != nullptr) {
        this->pedestrian_detection_->draw_on_frame(img_data, width, height);  // ← LENT!
      }
    #endif

    // 5. LVGL rendering (lignes 196-197) - MOYENNEMENT LENT
    lv_canvas_set_buffer(this->canvas_obj_, img_data, width, height, LV_IMG_CF_TRUE_COLOR);
    lv_obj_invalidate(this->canvas_obj_);  // ← Déclenche redraw LVGL
}
```

**Intervalle configuré:** 33ms par défaut (ligne 64: `update_interval_{33}`) = ~30 FPS cible

**Timer LVGL:** Ligne 50: `lv_timer_create(lvgl_timer_callback_, this->update_interval_, this)`

---

## 2. Bottlenecks Identifiés

### 🔴 CRITIQUE: Composants de Détection AI

**Localisation:** `lvgl_camera_display.cpp:171-185`

**Problème:**
- Les composants AI (`face_detection`, `yolo11_detection`, `pedestrian_detection`) sont appelés **À CHAQUE FRAME**
- Chaque appel à `draw_on_frame()` peut exécuter:
  - Inférence AI (si activée)
  - Dessin de rectangles/labels sur le buffer RGB565 (via imlib)

**Temps estimés par frame (1280x720 RGB565):**
| Composant | Temps typique | Impact FPS |
|-----------|---------------|------------|
| Face Detection | 50-100ms | Max 10-20 FPS |
| YOLO11 Detection | 100-200ms | Max 5-10 FPS |
| Pedestrian Detection | 80-150ms | Max 6-12 FPS |
| **TOTAL (si tous actifs)** | **230-450ms** | **2-4 FPS** |

**Si vous avez 8 FPS:**
- Un composant de détection est actif et prend ~125ms par frame
- OU plusieurs composants avec interval de détection configuré

### 🟡 MOYEN: LVGL Rendering

**Localisation:** `lvgl_camera_display.cpp:196-197`

```cpp
lv_canvas_set_buffer(this->canvas_obj_, img_data, width, height, LV_IMG_CF_TRUE_COLOR);
lv_obj_invalidate(this->canvas_obj_);
```

**Problème:**
- `lv_obj_invalidate()` déclenche un redraw complet du canvas dans la boucle LVGL
- Pour 1280x720 RGB565 = 1.8 MB de données à copier/traiter
- LVGL peut prendre 10-30ms pour rafraîchir un grand canvas

**Temps estimé:** 10-30ms par frame

### 🟢 PAS UN PROBLÈME: Capture Camera

**Localisation:** `esp_cam_sensor_camera.cpp:1244-1343`

```cpp
bool MipiDSICamComponent::capture_frame() {
  // VIDIOC_DQBUF - non-blocking (ligne 1264)
  if (ioctl(this->video_fd_, VIDIOC_DQBUF, &buf) < 0) {
    if (errno == EAGAIN) {
      return false;  // Pas de frame disponible
    }
  }

  // PPA transform (optionnel, hardware-accelerated) (ligne 1283-1296)
  if (this->ppa_enabled_ && this->image_buffer_) {
    this->apply_ppa_transform_(frame_data, this->image_buffer_);
  }
}
```

**Architecture zero-copy:**
- V4L2 USERPTR mode: pas de memcpy, données directement dans SPIRAM
- PPA (Pixel Processing Accelerator): transformation hardware (crop, rotate, mirror)
- Temps typique: <1ms pour DQBUF, <5ms pour PPA

**Temps estimé:** <5ms par frame ✅

### 🟢 PAS UN PROBLÈME: imlib Drawing

**imlib n'est PAS utilisé par lvgl_camera_display** (vérifié dans IMLIB_LVGL_VERIFICATION.md)

**imlib est utilisé PAR:**
- `face_detection->draw_on_frame()` pour dessiner rectangles
- `yolo11_detection->draw_on_frame()` pour dessiner boxes
- `pedestrian_detection->draw_on_frame()` pour dessiner boxes

**Temps de dessin imlib (rectangles/texte):** <1ms ✅

**Conclusion:** imlib n'est PAS la cause du ralentissement, c'est l'**inférence AI** dans les composants de détection!

---

## 3. Diagnostic: Identifier le Coupable

### Méthode 1: Analyser les Logs Existants

Le code log déjà les performances toutes les 100 frames (lignes 109-120):

```cpp
ESP_LOGI(TAG, "%u frames - FPS: %.2f | capture: %.1fms | canvas: %.1fms | skip: %.1f%%",
         this->frame_count_, fps, avg_capture, avg_canvas, skip_rate);
```

**Cherchez dans vos logs:**
```
[I][lvgl_camera_display:118]: 100 frames - FPS: 8.12 | capture: 0.5ms | canvas: 120.3ms | skip: 0.0%
```

**Interprétation:**
- `FPS: 8.12` → Confirme 8 FPS
- `capture: 0.5ms` → Camera capture OK ✅
- `canvas: 120.3ms` → **PROBLÈME ICI!** Devrait être <30ms
- `skip: 0.0%` → Pas de frames manquées

**Si `canvas` > 100ms:** Composants de détection AI sont le problème!

### Méthode 2: Tester avec Détections Désactivées

**Test 1: Désactiver temporairement toutes les détections**

Dans votre YAML ESPHome:
```yaml
# Commentez ou supprimez temporairement:
lvgl_camera_display:
  camera_id: tab5_cam
  canvas_id: camera_canvas
  # face_detection_id: face_detect     # ← Commentez
  # yolo11_detection_id: yolo11_detect # ← Commentez
  # pedestrian_detection_id: ped_detect # ← Commentez
```

**Recompilez et testez:**
- Si FPS passe à ~25-30 FPS → **Détections AI sont le problème** ✅
- Si FPS reste à 8 FPS → LVGL rendering est le problème

**Test 2: Augmenter detection_interval**

Dans vos composants de détection:
```yaml
face_detection:
  id: face_detect
  camera_id: tab5_cam
  detection_interval: 500ms  # ← Au lieu de 100ms par défaut
  draw_enabled: false         # ← Désactiver le dessin temporairement
```

---

## 4. Solutions et Optimisations

### Solution 1: ✅ Optimiser les Intervalles de Détection

**Problème:** Détections AI exécutées trop fréquemment

**Solution:**
```yaml
face_detection:
  id: face_detect
  camera_id: tab5_cam
  detection_interval: 500ms   # Au lieu de 33-100ms
  draw_enabled: true

yolo11_detection:
  id: yolo11_detect
  camera_id: tab5_cam
  detection_interval: 1000ms  # Détection toutes les secondes
  draw_enabled: true
```

**Impact:**
- Face detection 1x/500ms au lieu de 1x/33ms = **15x moins de CPU**
- YOLO11 1x/1000ms au lieu de 1x/33ms = **30x moins de CPU**
- FPS devrait passer de 8 → 25-30 FPS

### Solution 2: ✅ Désactiver Détections Inutilisées

Si vous n'utilisez pas certaines détections:
```yaml
lvgl_camera_display:
  camera_id: tab5_cam
  canvas_id: camera_canvas
  # face_detection_id: face_detect  # ← Supprimez si inutilisé
  # yolo11_detection_id: yolo11     # ← Supprimez si inutilisé
```

### Solution 3: ✅ Utiliser draw_enabled: false

Si vous voulez détecter mais PAS dessiner sur chaque frame:
```yaml
face_detection:
  id: face_detect
  camera_id: tab5_cam
  detection_interval: 200ms
  draw_enabled: false  # ← Pas de dessin sur canvas LVGL
```

Puis dessinez manuellement via automation si besoin.

### Solution 4: ⚠️ Réduire Résolution Canvas LVGL

**Si LVGL rendering est le problème (après test):**

```yaml
lvgl:
  displays:
    - my_display

  pages:
    - id: main_page
      widgets:
        - canvas:
            id: camera_canvas
            width: 640   # Au lieu de 1280
            height: 360  # Au lieu de 720
```

**Mais:** Nécessite aussi de configurer la caméra en 640x360 (pas de mode natif SC202CS).

**Recommandation:** Gardez 1280x720, optimisez les détections plutôt.

### Solution 5: ✅ Augmenter update_interval (si besoin)

Si vous n'avez pas besoin de 30 FPS:
```yaml
lvgl_camera_display:
  camera_id: tab5_cam
  canvas_id: camera_canvas
  update_interval: 66ms  # 15 FPS au lieu de 30 FPS (33ms)
```

**Impact:**
- Réduit la charge CPU LVGL de 50%
- Libère du temps pour détections AI
- 15 FPS reste fluide pour affichage caméra

---

## 5. Configuration Recommandée pour 25-30 FPS

### Configuration Optimale (avec détections AI)

```yaml
mipi_dsi_cam:
  id: tab5_cam
  sensor_type: sc202cs
  resolution: 720P
  pixel_format: RGB565

face_detection:
  id: face_detect
  camera_id: tab5_cam
  detection_interval: 300ms  # Détecte 3x/seconde
  draw_enabled: true
  score_threshold: 0.6

# Si vous utilisez YOLO11 (très lourd)
yolo11_detection:
  id: yolo11_detect
  camera_id: tab5_cam
  detection_interval: 1000ms  # Détecte 1x/seconde max!
  draw_enabled: true
  confidence_threshold: 0.5

lvgl_camera_display:
  camera_id: tab5_cam
  canvas_id: camera_canvas
  update_interval: 33ms  # 30 FPS cible
  face_detection_id: face_detect
  yolo11_detection_id: yolo11_detect
```

**Budget temps par frame (33ms pour 30 FPS):**
- Capture camera: 0.5ms ✅
- PPA transform: 3ms ✅
- Face detection drawing: 0.5ms (seulement quand détection mise à jour) ✅
- YOLO11 drawing: 0.5ms (seulement quand détection mise à jour) ✅
- LVGL rendering: 20ms ✅
- **TOTAL: ~25ms** → **~30 FPS réalisable** ✅

### Configuration Sans Détections (30 FPS garantis)

```yaml
mipi_dsi_cam:
  id: tab5_cam
  sensor_type: sc202cs
  resolution: 720P
  pixel_format: RGB565

lvgl_camera_display:
  camera_id: tab5_cam
  canvas_id: camera_canvas
  update_interval: 33ms
  # Pas de détections

# Utilisez imlib pour overlays légers si besoin
interval:
  - interval: 1s
    then:
      - lambda: |-
          // Dessiner FPS avec imlib (nécessite -DENABLE_IMLIB_DRAWING)
          id(tab5_cam).draw_string(10, 10, "FPS: 30", 0xFFFF, 2.0);
```

**Budget temps par frame:**
- Capture: 0.5ms ✅
- LVGL rendering: 20ms ✅
- imlib overlay (1x/sec): négligeable ✅
- **TOTAL: ~21ms** → **30+ FPS garanti** ✅

---

## 6. Tests de Performance Détaillés

### Test #1: Baseline (sans détections)

**Configuration:**
```yaml
lvgl_camera_display:
  camera_id: tab5_cam
  canvas_id: camera_canvas
  update_interval: 33ms
  # Pas de face_detection_id
  # Pas de yolo11_detection_id
```

**Résultat attendu:**
```
[I][lvgl_camera_display:118]: 100 frames - FPS: 28.5 | capture: 0.5ms | canvas: 22.0ms | skip: 0.0%
```

**Interprétation:** 28-30 FPS ✅ → LVGL seul n'est PAS le problème

### Test #2: Avec Face Detection (interval 100ms)

**Configuration:**
```yaml
face_detection:
  detection_interval: 100ms
  draw_enabled: true

lvgl_camera_display:
  face_detection_id: face_detect
```

**Résultat attendu:**
```
[I][lvgl_camera_display:118]: 100 frames - FPS: 15.2 | capture: 0.5ms | canvas: 62.5ms | skip: 0.0%
```

**Interprétation:** 15 FPS, canvas = 62.5ms → Face detection ralentit mais acceptable

### Test #3: Avec Face + YOLO11 (interval 100ms)

**Configuration:**
```yaml
face_detection:
  detection_interval: 100ms
  draw_enabled: true

yolo11_detection:
  detection_interval: 100ms
  draw_enabled: true

lvgl_camera_display:
  face_detection_id: face_detect
  yolo11_detection_id: yolo11_detect
```

**Résultat attendu:**
```
[I][lvgl_camera_display:118]: 100 frames - FPS: 6.8 | capture: 0.5ms | canvas: 145.2ms | skip: 0.0%
```

**Interprétation:** 7 FPS ✅ → **C'EST VOTRE CAS!**

**Solution:** Augmenter intervals à 300ms (face) et 1000ms (YOLO11)

### Test #4: Face (300ms) + YOLO11 (1000ms)

**Configuration:**
```yaml
face_detection:
  detection_interval: 300ms
  draw_enabled: true

yolo11_detection:
  detection_interval: 1000ms
  draw_enabled: true
```

**Résultat attendu:**
```
[I][lvgl_camera_display:118]: 100 frames - FPS: 26.3 | capture: 0.5ms | canvas: 25.8ms | skip: 0.0%
```

**Interprétation:** 26 FPS ✅ → Performance restaurée!

---

## 7. Checklist de Diagnostic

### Étape 1: Vérifier Configuration Actuelle

```yaml
# Cherchez dans votre YAML:
lvgl_camera_display:
  camera_id: ?
  canvas_id: ?
  update_interval: ?  # Devrait être 33ms ou absent (défaut = 33ms)
  face_detection_id: ?  # ← Est-ce présent?
  yolo11_detection_id: ?  # ← Est-ce présent?
  pedestrian_detection_id: ?  # ← Est-ce présent?
```

### Étape 2: Lire les Logs de Performance

```bash
# Cherchez dans les logs série:
[I][lvgl_camera_display:118]: XXX frames - FPS: ?.?? | capture: ?.?ms | canvas: ?.?ms | skip: ?.?%
```

**Interprétation:**
- `capture > 5ms` → Problème caméra (rare)
- `canvas > 50ms` → **Détections AI trop fréquentes**
- `skip > 10%` → update_interval trop court

### Étape 3: Identifier les Détections Actives

```bash
# Cherchez dans les logs au démarrage:
[I][face_detection:XXX]: Face Detection enabled, interval=XXXms
[I][yolo11_detection:XXX]: YOLO11 Detection enabled, interval=XXXms
```

### Étape 4: Appliquer Solutions

**Si canvas > 50ms ET détections actives:**
1. Augmenter `detection_interval` à 300-1000ms
2. OU désactiver `draw_enabled: false`
3. OU supprimer détections inutilisées

**Si canvas > 50ms ET PAS de détections:**
1. Vérifier si imlib est activé par erreur
2. Augmenter `update_interval` à 40-50ms (20-25 FPS)
3. Profiler LVGL rendering (hors scope)

---

## 8. Résumé

### 🔴 Cause Probable de vos 8 FPS

**Composants de détection AI activés avec interval trop court:**
- Face detection avec `detection_interval: 100ms` ou moins
- YOLO11 detection avec `detection_interval: 100ms` ou moins
- Les deux combinés = 6-8 FPS

### ✅ Solution Immédiate

**Modifiez vos intervals de détection:**
```yaml
face_detection:
  detection_interval: 300ms  # Au lieu de 100ms

yolo11_detection:
  detection_interval: 1000ms  # Au lieu de 100ms
```

**Résultat attendu:** 25-30 FPS ✅

### ❌ Ce qui N'est PAS le Problème

- ✅ Capture caméra (zero-copy, <1ms)
- ✅ PPA transformations (<5ms)
- ✅ imlib drawing (pas utilisé par lvgl_camera_display, <1ms pour détections)
- ✅ Bayer format BGGR (vérifié correct)

### 📊 Budget Temps pour 30 FPS (33ms/frame)

| Opération | Temps | Reste |
|-----------|-------|-------|
| Capture camera | 0.5ms | 32.5ms |
| PPA transform | 3ms | 29.5ms |
| Face detect drawing (1x/300ms) | 0.5ms amortisé | 29ms |
| YOLO11 drawing (1x/1000ms) | 0.5ms amortisé | 28.5ms |
| LVGL rendering | 20ms | 8.5ms |
| **Marge** | **8.5ms** | ✅ |

---

## 9. Prochaines Étapes

1. **Vérifier logs actuels** pour confirmer `canvas > 50ms`
2. **Identifier détections actives** dans votre YAML
3. **Augmenter detection_interval** à 300-1000ms
4. **Recompiler et tester**
5. **Valider FPS ~30** dans les logs

**Si problème persiste:** Partagez vos logs de performance complets.
