# Analyse des Performances MP4 sur ESP32-P4 avec LVGL v8

## 🔍 Problème Identifié

**Situation actuelle:**
- **MJPEG (JPEG hardware):** 30+ FPS ✅
- **MP4/H.264 Baseline (tinyh264):** 7-8 FPS ❌
- **MP4/H.264 High Profile (OpenH264):** Probablement 4-5 FPS ❌

**Différence:** MJPEG est **4-6x plus rapide** que H.264 !

## 📊 Profiling du Pipeline H.264

### Pipeline Actuel (RTSP/MP4 H.264)

```
┌────────────────────────────────────────────────────────────────┐
│ 1. Lecture Stream H.264                                        │
│    ↓ 0.5-1ms (réseau/fichier)                                  │
├────────────────────────────────────────────────────────────────┤
│ 2. Décodage H.264 SOFTWARE (tinyh264/OpenH264)                │
│    ↓ 80-150ms ← GOULOT D'ÉTRANGLEMENT #1                      │
│    • Baseline (tinyh264): ~80-100ms                            │
│    • High Profile (OpenH264): ~120-150ms                       │
├────────────────────────────────────────────────────────────────┤
│ 3. Format YUV420 (I420)                                        │
│    ↓ 0ms (pas de copie, pointeur direct)                       │
├────────────────────────────────────────────────────────────────┤
│ 4. Conversion YUV420 → RGB565                                  │
│    ↓ 25-35ms ← GOULOT D'ÉTRANGLEMENT #2                       │
│    • Software conversion (boucles CPU)                         │
│    • 1280×720: 921,600 pixels × 3 opérations                  │
├────────────────────────────────────────────────────────────────┤
│ 5. Copie vers buffer LVGL                                      │
│    ↓ 5-10ms (memcpy 1.8MB)                                     │
├────────────────────────────────────────────────────────────────┤
│ 6. Rendu LVGL v8                                               │
│    ↓ 15-25ms (rafraîchissement écran)                         │
└────────────────────────────────────────────────────────────────┘

TOTAL: 125-220ms → 4.5-8 FPS
```

### Pipeline MJPEG (comparaison)

```
┌────────────────────────────────────────────────────────────────┐
│ 1. Lecture Stream MJPEG                                        │
│    ↓ 0.5-1ms                                                   │
├────────────────────────────────────────────────────────────────┤
│ 2. Décodage JPEG HARDWARE                                      │
│    ↓ 18-25ms ← HARDWARE ACCELERÉ                              │
│    • Direct vers RGB565                                        │
│    • Pas de conversion YUV nécessaire                          │
├────────────────────────────────────────────────────────────────┤
│ 3. Copie vers buffer LVGL (optionnel)                         │
│    ↓ 5-10ms                                                    │
├────────────────────────────────────────────────────────────────┤
│ 4. Rendu LVGL v8                                               │
│    ↓ 15-25ms                                                   │
└────────────────────────────────────────────────────────────────┘

TOTAL: 38-60ms → 16-26 FPS
```

## 🎯 Pourquoi H.264 est si lent ?

### 1. Pas de décodeur H.264 hardware

ESP32-P4 a:
- ✅ Décodeur **JPEG hardware** (très rapide)
- ✅ Encodeur **H.264 hardware** (pour enregistrement)
- ❌ **PAS** de décodeur H.264 hardware

**Résultat:** Décodage H.264 100% CPU software = TRÈS LENT

### 2. Complexité algorithmes H.264

| Opération | Baseline | High Profile |
|-----------|----------|--------------|
| **Motion Compensation** | Simple | Complexe (1/4 pixel) |
| **Deblocking Filter** | Simple | Avancé |
| **Entropy Decoding** | CAVLC | CABAC (2x plus lent) |
| **8×8 Transform** | Non | Oui |
| **Weighted Prediction** | Non | Oui |

**OpenH264 High Profile = 1.5-2x plus lent que tinyh264 Baseline**

### 3. Conversion YUV → RGB

```c
// Boucle pour chaque pixel (exemple simplifié)
for (int i = 0; i < width * height; i++) {
    // Lire Y, U, V (3 accès mémoire)
    uint8_t y = yuv_buffer[i];
    uint8_t u = yuv_buffer[y_size + i/4];
    uint8_t v = yuv_buffer[y_size + uv_size + i/4];

    // Calculs (6 multiplications, 6 additions)
    int r = y + 1.402 * (v - 128);
    int g = y - 0.344 * (u - 128) - 0.714 * (v - 128);
    int b = y + 1.772 * (u - 128);

    // Clamping (3 opérations)
    r = clamp(r, 0, 255);
    g = clamp(g, 0, 255);
    b = clamp(b, 0, 255);

    // Conversion RGB888 → RGB565 (3 shifts, 3 masks, 2 ORs)
    rgb565_buffer[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}
```

**Pour 640×480:**
- 307,200 pixels × ~20 opérations = 6,144,000 opérations
- @ 240MHz CPU = ~25-30ms

### 4. LVGL v8 consomme du CPU

LVGL v8 utilise beaucoup de CPU pour:
- Anti-aliasing
- Blending
- Widget rendering
- Event handling

**Moins de CPU disponible pour décodage vidéo**

## 💡 Solutions Proposées

### Solution 1: Convertir MP4 → MJPEG (RECOMMANDÉ ⭐)

**Avantages:**
- Utilise le décodeur JPEG **hardware** (30+ FPS garanti)
- Pas de conversion YUV nécessaire
- Décodage 5-7x plus rapide que H.264 software

**Comment faire:**

#### A. Avec FFmpeg (sur PC)

```bash
# Haute qualité (JPEG quality 2-5)
ffmpeg -i input.mp4 \
  -c:v mjpeg \
  -q:v 3 \
  -vf "scale=640:480" \
  -r 30 \
  output_mjpeg.avi

# Taille optimisée (JPEG quality 10-15)
ffmpeg -i input.mp4 \
  -c:v mjpeg \
  -q:v 12 \
  -vf "scale=640:480" \
  -r 25 \
  output_mjpeg.avi

# Très petite taille (JPEG quality 20-25)
ffmpeg -i input.mp4 \
  -c:v mjpeg \
  -q:v 20 \
  -vf "scale=480:320" \
  -r 20 \
  output_mjpeg.avi
```

**Paramètres:**
- `-q:v 3-5`: Excellente qualité, ~80-120KB/frame
- `-q:v 10-15`: Bonne qualité, ~40-60KB/frame
- `-q:v 20-25`: Acceptable, ~20-30KB/frame

#### B. Configuration ESPHome

```yaml
video_player:
  - id: my_video_player
    file: "/sdcard/video_mjpeg.avi"  # Fichier converti en MJPEG
    canvas_id: main_canvas
    loop: true
    autoplay: true
```

**Résultat attendu:** 25-30 FPS stable

---

### Solution 2: Optimiser Pipeline H.264 (si MJPEG impossible)

Si vous devez absolument utiliser MP4/H.264:

#### A. Réduire drastiquement la résolution

```yaml
network_camera:
  - id: cam
    url: "rtsp://ip/stream2"  # Utiliser stream2 (basse résolution)
    width: 320
    height: 240  # Très petite résolution
    canvas_id: canvas
```

**Gain:**
- 320×240 vs 640×480 = 4x moins de pixels
- Décodage H.264: ~80ms → ~20ms
- Conversion YUV: ~25ms → ~6ms
- **Total: ~26ms → 38 FPS**

#### B. Activer optimisations CPU

```c
// Dans sdkconfig
CONFIG_FREERTOS_HZ=1000                    // Tick rate 1000Hz (plus réactif)
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360=y      // CPU 360MHz (si supporté)
CONFIG_COMPILER_OPTIMIZATION_PERF=y        // Optimisation -O3

// Dans CMakeLists.txt de esp_h264
target_compile_options(esp_h264 PRIVATE
    -O3                    # Optimisation maximale
    -funroll-loops         # Dérouler les boucles
    -ftree-vectorize       # Vectorisation automatique
    -ffast-math            # Maths rapides
)
```

#### C. Utiliser conversion YUV hardware (si disponible)

Vérifier si ESP32-P4 a un convertisseur YUV→RGB hardware via PPA:

```c
// Utiliser PPA pour YUV→RGB au lieu de software
ppa_srm_oper_t ppa_config = {
    .mode = PPA_TRANS_MODE_NON_BLOCKING,
    .color_mode = PPA_COLOR_MODE_YUV420,  // Input
    .output_color_mode = PPA_COLOR_MODE_RGB565,  // Output
    // ...
};
```

**Gain potentiel:** 25ms → 5-10ms

---

### Solution 3: Serveur MP4→MJPEG en temps réel

Si vous avez un serveur (Raspberry Pi, NAS, PC):

#### A. Installer serveur de conversion

```bash
# Sur serveur Linux
ffmpeg -re -i video.mp4 \
  -c:v mjpeg \
  -q:v 10 \
  -f mjpeg \
  http://localhost:8080/stream.mjpg
```

#### B. Configuration ESPHome

```yaml
network_camera:
  - id: cam
    url: "http://192.168.1.100:8080/stream.mjpg"
    protocol: mjpeg  # MJPEG via HTTP
    width: 640
    height: 480
    canvas_id: canvas
```

**Avantages:**
- Garde fichiers MP4 originaux
- Conversion en temps réel côté serveur
- ESP32-P4 reçoit du MJPEG (rapide)

---

### Solution 4: Désactiver/Réduire LVGL v8

```c
// Réduire la charge LVGL
lv_disp_set_draw_buffers(disp, buf1, NULL);  // 1 seul buffer au lieu de 2
lv_obj_set_style_opa(obj, LV_OPA_COVER, 0); // Désactiver transparence
lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE); // Désactiver scroll
```

**Gain:** ~5-10ms par frame

---

## 📈 Tableau Comparatif Final

| Solution | FPS @ 640×480 | FPS @ 320×240 | Qualité | Difficulté |
|----------|---------------|---------------|---------|------------|
| **MP4 H.264 Baseline (actuel)** | 7-8 | 15-18 | Excellente | - |
| **MP4 H.264 High (OpenH264)** | 4-5 | 10-12 | Excellente | Moyenne |
| **MJPEG hardware (converti)** | **25-30** | **30+** | Excellente | Facile |
| **H.264 + optimisations CPU** | 10-12 | 25-30 | Excellente | Difficile |
| **H.264 + YUV hardware** | 15-18 | 30+ | Excellente | Moyenne |
| **Serveur MP4→MJPEG** | **25-30** | **30+** | Excellente | Moyenne |

## 🎯 Recommandation Finale

### Pour vidéos locales (SD card):
```bash
# Convertir une fois avec FFmpeg
ffmpeg -i video.mp4 -c:v mjpeg -q:v 10 -vf "scale=640:480" -r 25 video.avi
```

### Pour streaming réseau (caméra IP):
```yaml
# Utiliser MJPEG directement
network_camera:
  url: "http://camera_ip/mjpeg_stream"
  protocol: mjpeg
```

### Si H.264 obligatoire:
1. **Réduire à 320×240** minimum
2. Utiliser **stream2** (basse résolution) de la caméra
3. Accepter **10-15 FPS** maximum

---

## 🔬 Tests à Effectuer

### Test 1: MJPEG converti

```bash
# Convertir un MP4 de test
ffmpeg -i test.mp4 -c:v mjpeg -q:v 10 -t 10 test_mjpeg.avi

# Mesurer FPS
# Logs ESP32-P4 devraient montrer ~25-30 FPS
```

### Test 2: H.264 320×240

```yaml
video_player:
  width: 320
  height: 240
# Mesurer FPS - devrait être ~20-25 FPS
```

### Test 3: Optimisations compiler

```cmake
# Ajouter dans CMakeLists.txt
target_compile_options(esp_h264 PRIVATE -O3 -funroll-loops)
# Recompiler et mesurer gain
```

---

## 📚 Ressources

- **FFmpeg Documentation:** https://ffmpeg.org/ffmpeg-formats.html#mjpeg
- **ESP32-P4 JPEG Decoder:** https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/jpeg.html
- **LVGL Performance:** https://docs.lvgl.io/master/overview/renderin.html

---

**Conclusion:** Utilisez **MJPEG** ! C'est la solution la plus simple et la plus rapide pour ESP32-P4.
