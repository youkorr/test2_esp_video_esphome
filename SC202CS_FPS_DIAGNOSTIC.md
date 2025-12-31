# SC202CS FPS Diagnostic - 7.36 FPS au lieu de 30 FPS

## 📊 Statistiques Actuelles

```
[I][lvgl_camera_display:118]: 200 frames - FPS: 7.36 | capture: 23.1ms | canvas: 0.4ms | skip: 0.0%
```

### Analyse des Temps

| Opération | Temps | Budget pour 30 FPS |
|-----------|-------|---------------------|
| **capture** | 23.1ms | ✅ OK (33ms disponibles) |
| **canvas** | 0.4ms | ✅ Très rapide |
| **TOTAL mesuré** | 23.5ms | ✅ Devrait donner 42 FPS ! |
| **TOTAL réel** | 136ms (7.36 FPS) | ❌ **112ms manquants !** |
| **skip rate** | 0.0% | ℹ️ Aucune frame manquée |

## 🔍 Diagnostic du Problème

### Problème #1: LVGL Software Crop (CRITIQUE)

**Configuration actuelle:**
- Canvas: **800x480** pixels
- Image camera: **800x600** pixels
- Différence: **120 pixels** en hauteur doivent être croppés

**Impact:**
- LVGL doit faire un **crop software** de 800x600 → 800x480
- Opération coûteuse: manipulation de **800 × 600 × 2 = 960 KB** de données
- Même si `canvas: 0.4ms` semble rapide, le crop se fait **ailleurs dans LVGL**

**Référence M5Stack:**
```cpp
// M5Stack utilise PPA hardware pour resize AVANT LVGL
uint8_t *buffer[2];  // Double buffering
ppa_scale_rotate_mirror(src, dst, width, height);  // Hardware!
```

### Problème #2: LVGL Display Refresh

Le code `lvgl_camera_display.cpp` mesure seulement:
1. `capture_frame()` → 23.1ms
2. `lv_canvas_set_buffer()` → 0.4ms

**Mais ne mesure PAS:**
- `lv_obj_invalidate()` → déclenche refresh LVGL
- LVGL `lv_timer_handler()` → traitement des événements
- LVGL `lv_refr_now()` → refresh display
- **LVGL software crop** → 112ms cachés ici !

### Pourquoi skip: 0.0% ?

Le fait que `skip = 0.0%` prouve que:
- Le timer LVGL est appelé **exactement au rythme des frames disponibles**
- Il n'y a **jamais** de tentative échouée de capture
- Cela signifie que LVGL est le **bottleneck**, pas la caméra !

Si la caméra était lente, on verrait des skips quand le timer demande une frame trop tôt.

## 🎯 Solutions (par ordre de priorité)

### Solution #1: PPA Hardware Resize (CRITIQUE)

**Ajouter dans votre configuration mipi_dsi_cam:**

```yaml
mipi_dsi_cam:
  id: tab5_cam
  sensor_type: sc202cs
  i2c_id: bsp_bus
  sensor_addr: 0x36
  resolution: "800x600"      # ✅ Capture en 800x600
  pixel_format: RGB565
  framerate: 30

  # 🔧 AJOUTER CES DEUX LIGNES (solution PPA hardware)
  output_width: 800          # ← Resize hardware 800x600 → 800x480
  output_height: 480         # ← Évite le crop software lent de LVGL
```

**Résultat attendu:**
- PPA (Picture Processing Accelerator) fait le resize en **hardware**
- LVGL reçoit directement 800x480 → **pas de crop software**
- FPS devrait passer de **7.36 → 30 FPS**

### Solution #2: Double Buffering (comme M5Stack)

M5Stack utilise 2 buffers pour éviter les contentions:

```cpp
#define EXAMPLE_VIDEO_BUFFER_COUNT 2
uint8_t *buffer[2];
```

**Vérifier dans votre configuration:**
```yaml
mipi_dsi_cam:
  # ...
  buffer_count: 2  # Au minimum (ou 4 pour meilleure performance)
```

### Solution #3: Optimisation LVGL (si Solutions 1-2 ne suffisent pas)

**Option A: Réduire canvas à résolution native**
```yaml
# Dans LVGL page:
widgets:
  - canvas:
      width: 800
      height: 600  # ← Au lieu de 480
      # Problème: dépassera l'écran de 120px
```

**Option B: Réduire résolution camera**
```yaml
mipi_dsi_cam:
  resolution: "640x480"  # VGA standard (ratio 4:3 = 800x600)
  output_width: 800      # PPA upscale → 800x600
  output_height: 600     # Puis canvas crop → 800x480
```

## 📈 Comparaison avec M5Stack

### M5Stack hal_camera.cpp
```cpp
vTaskDelay(pdMS_TO_TICKS(10));  // 100 FPS max
buffer_count = 2                 // Double buffering
MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM  // Buffers en SPIRAM
PPA scale_rotate_mirror          // Hardware transform
```

### ESPHome actuel
```cpp
lv_timer_create(callback, 33ms)  // 30 FPS théorique
buffer_count = ? (à vérifier)     // Configuration utilisateur
SPIRAM ✅                         // Déjà OK
PPA = ❌ NON CONFIGURÉ !          // ← PROBLÈME !
```

## 🔧 Actions Immédiates

1. **AJOUTER `output_width: 800` et `output_height: 480`** dans votre config mipi_dsi_cam
2. Recompiler et flasher
3. Vérifier les logs:
   ```
   [I][lvgl_camera_display]: 100 frames - FPS: 30.0 | capture: 23ms | canvas: 0.4ms
   ```

4. Si FPS toujours faible:
   - Vérifier `buffer_count` (doit être ≥ 2)
   - Chercher warnings LVGL dans logs
   - Profiler avec `esp_log_level_set("lvgl", ESP_LOG_DEBUG)`

## 📚 Références

- [M5Stack hal_camera.cpp](https://github.com/m5stack/M5Tab5-UserDemo/blob/main/platforms/tab5/main/hal/components/hal_camera.cpp)
- Documentation PPA hardware resize déjà ajoutée dans `LVGL_CAMERA_PAGE_SC202CS.yaml:175-177`
- Comparaison OV5647/OV02C10 qui n'ont PAS ce problème (résolutions natives compatibles)

## ⚠️ Note Importante

Ce problème est **spécifique au SC202CS** car:
- OV5647: 1280x720 native → crop/resize matche mieux l'écran
- OV02C10: idem
- **SC202CS**: 800x600 → **mismatch 120px** avec écran 800x480

Sans PPA resize, LVGL doit cropper 120px en software à **chaque frame** → bottleneck !
