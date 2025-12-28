# Fix: Effet de Zoom x2 sur OV02C10 640x480

## 🔍 Problème Identifié

Le format **640x480 (VGA 4:3)** sur OV02C10 **perd 25% du champ de vision horizontal** à cause du crop nécessaire pour passer du ratio natif du capteur (16:9) au ratio 4:3.

### Analyse Technique

| Paramètre | Valeur | Description |
|-----------|--------|-------------|
| **Capteur natif** | 1936×1088 | Ratio 1.78:1 (proche de 16:9) |
| **Format demandé** | 640×480 | Ratio 1.33:1 (4:3) |
| **Crop horizontal** | **485 pixels (25%)** | ❌ Perte de FOV |
| **Zoom visuel** | **1.33x** | On ne voit que 75% de la scène |

### Registres Actuels (640x480)

```c
// Crop window: FULL SENSOR (0-1935 x 4-1091) - registres OK
{0x3800, 0x00}, {0x3801, 0x00},  // X start = 0
{0x3802, 0x00}, {0x3803, 0x04},  // Y start = 4
{0x3804, 0x07}, {0x3805, 0x8f},  // X end = 1935
{0x3806, 0x04}, {0x3807, 0x43},  // Y end = 1091

// Output size: 640x480 (ISP downscales from 1936x1087)
{0x3808, 0x02}, {0x3809, 0x80},  // width = 640
{0x380a, 0x01}, {0x380b, 0xe0},  // height = 480
```

**Le problème :** L'ISP downscale de 1936×1088 → 640×480 en croppant 25% horizontal pour obtenir le ratio 4:3.

---

## ✅ SOLUTION RECOMMANDÉE: Format 640×368

Utilisez le format **640×368** qui existe déjà dans le code et conserve **98% du FOV** au lieu de 75%.

### Comparaison 640×480 vs 640×368

| Format | Ratio | Crop Horizontal | FOV Conservé | Zoom Visuel |
|--------|-------|-----------------|--------------|-------------|
| **640×480** | 4:3 (1.33) | **25.1%** ❌ | 75% | 1.33x |
| **640×368** | ~16:9 (1.74) | **2.3%** ✅ | 98% | 1.02x |

### Configuration ESPHome

```yaml
esp_video:
  id: my_cam
  i2c_id: bsp_bus
  sensor_type: ov02c10
  sensor_addr: 0x36

  # ✅ Utilisez 640x368 au lieu de 640x480
  resolution: "640x368"  # Near 16:9, conserve 98% du FOV

  pixel_format: RGB565
  framerate: 30

lvgl_camera_display:
  id: camera_display
  camera_id: my_cam
  canvas_id: camera_canvas
  update_interval: 100ms
```

### Configuration LVGL Canvas

```yaml
lvgl:
  pages:
    - id: camera_page
      widgets:
        - canvas:
            id: camera_canvas
            width: 640
            height: 368   # ← Changez de 480 → 368
            x: 80         # Centré sur écran 800x480
            y: 56         # Centré verticalement: (480-368)/2 = 56
```

### Avantages Format 640×368

1. ✅ **98% du FOV conservé** (vs 75% pour 640×480)
2. ✅ **Ratio natif du capteur** (proche de 16:9)
3. ✅ **16-byte aligned** (368 % 16 = 0) → Compatible rotation ISP
4. ✅ **Pas de zoom visuel** (seulement 1.02x vs 1.33x)
5. ✅ **Format déjà implémenté** dans le code

---

## 🔧 SOLUTION ALTERNATIVE 1: PPA Resize depuis 1920×1080

Si vous avez ABSOLUMENT besoin de 640×480 (ratio 4:3), utilisez le format 1920×1080 + PPA resize.

### Configuration avec PPA

```yaml
esp_video:
  resolution: "1920x1080"  # Full sensor, 100% FOV
  pixel_format: RGB565
  framerate: 30

  # PPA resize à 640x480
  output_width: 640
  output_height: 480
  enable_ppa: true
```

### Avantages PPA Resize

1. ✅ **100% du FOV conservé** (full sensor)
2. ✅ **Ratio 4:3 exact** (640×480)
3. ❌ **CPU overhead** (PPA resize)
4. ❌ **Plus de RAM** (capture 1920×1080 puis resize)

---

## 🔧 SOLUTION ALTERNATIVE 2: Modifier Registres 640×480 (Letterbox)

Modifiez les registres pour garder le ratio 16:9 du capteur et ajouter du letterboxing.

### Principe

- Garder **100% du FOV horizontal** (1936 pixels)
- Réduire la hauteur pour obtenir 640 pixels de large avec ratio 16:9
- Hauteur effective: 640 / 1.78 = **360 pixels**
- Ajouter **60 pixels de bandes noires** (30 en haut, 30 en bas)

### Nouveaux Registres (à implémenter)

```c
// Crop window: FULL WIDTH (0-1935), hauteur ajustée pour ratio 16:9
{0x3800, 0x00}, {0x3801, 0x00},  // X start = 0 (full width)
{0x3802, 0x00}, {0x3803, 0x8c},  // Y start = 140 (crop 70px top)
{0x3804, 0x07}, {0x3805, 0x8f},  // X end = 1935 (full width)
{0x3806, 0x03}, {0x3807, 0xb8},  // Y end = 952 (crop 70px bottom)

// Output size: 640x360 (ratio 16:9, letterbox to 640x480)
{0x3808, 0x02}, {0x3809, 0x80},  // width = 640
{0x380a, 0x01}, {0x380b, 0x68},  // height = 360
```

**Note:** Cette solution nécessite de modifier le fichier `ov02c10_settings.h` et d'ajouter un nouveau format.

---

## 📊 Comparaison des Solutions

| Solution | FOV Conservé | Ratio | CPU Overhead | Modifications Code |
|----------|--------------|-------|--------------|-------------------|
| **640×368** (recommandé) | ✅ 98% | 16:9 | ✅ Aucun | ✅ Aucune |
| **1920×1080 + PPA** | ✅ 100% | 4:3 | ⚠️ Moyen | ✅ Config seulement |
| **640×480 letterbox** | ✅ 100% horizontal | 16:9 | ✅ Aucun | ❌ Registres |
| **640×480 actuel** | ❌ 75% | 4:3 | ✅ Aucun | - |

---

## 🎯 Recommandation Finale

**Utilisez le format 640×368** sauf si vous avez une contrainte stricte de ratio 4:3.

### Pourquoi 640×368 ?

1. **Déjà implémenté** - Pas de modifications de code nécessaires
2. **98% du FOV** - Presque aucun crop
3. **Alignement ISP** - Compatible rotation hardware
4. **Performance optimale** - Pas de PPA overhead
5. **Ratio natif** - Correspond au capteur

### Migration Rapide

```bash
# Cherchez dans votre YAML:
resolution: "640x480"

# Remplacez par:
resolution: "640x368"

# Ajustez le canvas LVGL:
height: 368
y: 56  # Centré sur écran 800x480
```

---

## 📝 Exemples Complets

### Exemple 1: Camera Page avec 640×368

```yaml
lvgl:
  pages:
    - id: camera_page
      bg_color: 0x000000

      widgets:
        # Canvas centré sur écran 800×480
        - canvas:
            id: camera_canvas
            width: 640
            height: 368
            x: 80   # (800-640)/2 = 80
            y: 56   # (480-368)/2 = 56
            bg_color: 0x000000

        # Bouton RETOUR
        - button:
            id: btn_back
            width: 70
            height: 45
            x: 5
            y: 10
            on_click:
              then:
                - lambda: id(my_cam).stop_streaming();
                - lvgl.page.show: page_home
            widgets:
              - label:
                  text: "BACK"
                  text_color: 0xFFFFFF

esp_video:
  id: my_cam
  i2c_id: bsp_bus
  sensor_type: ov02c10
  resolution: "640x368"  # ← Format recommandé
  pixel_format: RGB565
  framerate: 30

lvgl_camera_display:
  id: camera_display
  camera_id: my_cam
  canvas_id: camera_canvas
  update_interval: 100ms
```

### Exemple 2: Face Recognition avec 640×368

```yaml
esp_video:
  id: face_cam
  i2c_id: i2c_bus
  sensor_type: ov02c10
  resolution: "640x368"  # 98% FOV - Meilleur pour détection faciale
  pixel_format: RGB565
  framerate: 30

# Face recognition component
face_recognition:
  camera_id: face_cam
  # Le modèle AI peut travailler avec 640×368 sans problème
```

---

## 🔗 Fichiers Concernés

- **Format déjà implémenté:** `components/esp_cam_sensor/sensor/ov02c10/ov02c10.c:1185-1212`
- **Registres 640×368:** `components/esp_cam_sensor/sensor/ov02c10/private_include/ov02c10_settings.h:1223-1313`
- **Header custom formats:** `components/esp_cam_sensor/ov02c10_custom_formats.h:39-40`

---

## ✅ Vérification

Après modification, vérifiez dans les logs:

```
✅ Using CUSTOM format: 640x368 RAW10 @ 30fps (near 16:9, ~2% crop, 16-byte aligned!)
```

L'image doit maintenant montrer **98% de la scène** au lieu de 75% !
