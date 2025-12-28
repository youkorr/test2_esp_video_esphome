# Guide: Éviter le Zoom sur OV02C10 avec Rotation 270°

## 🔍 Votre Configuration Actuelle

```yaml
esp_cam_sensor:
  resolution: "640x480"  # ← Crop 25% du FOV horizontal
  rotation: 270
  ppa_enabled: true

lvgl:
  canvas:
    width: 480
    height: 390
```

### Problème Identifié

1. **Capture 640×480** → Crop **25% du FOV horizontal** (zoom 1.33x)
2. **Rotation 270°** → Devient 480×640
3. **PPA crop vertical** → 480×390 pour canvas

**Résultat:** Vous perdez 25% de la scène horizontalement AVANT même la rotation !

---

## ✅ SOLUTION 1: Adapter le Canvas (RECOMMANDÉ)

Changez le canvas pour s'adapter au format 640×368 après rotation (368×640).

### Configuration ESPHome

```yaml
esp_cam_sensor:
  id: tab5_cam
  i2c_id: i2c_bus
  sensor_type: ov02c10
  resolution: "640x368"  # ← Format 16:9, seulement 2% crop
  pixel_format: "RGB565"
  framerate: 30
  jpeg_quality: 15
  rotation: 270         # → Devient 368×640 après rotation
  mirror_x: true
  mirror_y: false
  crop_offset_x: 0
  ppa_enabled: true
```

### Canvas LVGL Adapté

```yaml
lvgl:
  pages:
    - id: camera_page
      bg_color: 0x1a1a1a
      widgets:
        - canvas:
            id: camera_canvas
            width: 368       # ← Réduit de 480 → 368
            height: 390      # ← Garde la même hauteur
            x: 216           # ← Centré: (800-368)/2 = 216
            y: 40
            bg_color: 0x000000
```

### Avantages
- ✅ **98% du FOV conservé** (vs 75% actuellement)
- ✅ **Pas de upscale** (meilleure qualité d'image)
- ✅ **PPA minimal** (640→390 vertical seulement)
- ✅ **Meilleure performance** (moins de resize)

### Inconvénients
- ⚠️ Canvas légèrement plus étroit (368px au lieu de 480px)
- ⚠️ Boutons à repositionner (plus d'espace à droite)

---

## ✅ SOLUTION 2: Format 1920×1080 (100% FOV)

Utilisez le format natif 1920×1080 pour **aucun crop**, puis PPA resize.

### Configuration ESPHome

```yaml
esp_cam_sensor:
  id: tab5_cam
  i2c_id: i2c_bus
  sensor_type: ov02c10
  resolution: "1920x1080"  # ← Full sensor, 0% crop
  pixel_format: "RGB565"
  framerate: 30
  jpeg_quality: 15
  rotation: 270            # → Devient 1080×1920 après rotation
  mirror_x: true
  mirror_y: false
  ppa_enabled: true
```

### Canvas LVGL (Identique)

```yaml
- canvas:
    id: camera_canvas
    width: 480
    height: 390
    x: 30
    y: 40
```

### Avantages
- ✅ **100% du FOV conservé** (aucun crop capteur)
- ✅ **Qualité maximale** (full resolution)
- ✅ **Garde votre canvas actuel** (480×390)

### Inconvénients
- ❌ **Plus de RAM** (~6.2 MB vs ~1.2 MB pour 640×480)
- ❌ **Plus de CPU** (PPA resize de 1080×1920 → 480×390)
- ❌ **Possible watchdog timeout** si CPU surchargé

---

## ✅ SOLUTION 3: Garder Canvas 480×390 avec 640×368

Acceptez un upscale horizontal minimal (368→480).

### Configuration ESPHome

```yaml
esp_cam_sensor:
  id: tab5_cam
  i2c_id: i2c_bus
  sensor_type: ov02c10
  resolution: "640x368"  # ← Format 16:9, seulement 2% crop
  pixel_format: "RGB565"
  framerate: 30
  jpeg_quality: 15
  rotation: 270         # → Devient 368×640 après rotation
  mirror_x: true
  mirror_y: false
  ppa_enabled: true
```

### Canvas LVGL (Identique)

```yaml
- canvas:
    id: camera_canvas
    width: 480       # ← Garde 480 (upscale de 368)
    height: 390      # ← Crop de 640
    x: 30
    y: 40
```

### Avantages
- ✅ **98% du FOV conservé** (vs 75% actuellement)
- ✅ **Garde votre canvas actuel** (480×390)
- ✅ **Pas de repositionnement** de boutons

### Inconvénients
- ⚠️ **Upscale horizontal** 368→480 (+30%)
- ⚠️ **Léger flou** possible à cause de l'upscale
- ⚠️ **Crop vertical** 640→390 (~39%)

**Note:** Même avec l'upscale, c'est MIEUX que perdre 25% de FOV !

---

## 📊 Comparaison des Solutions

| Solution | FOV Horizontal | Canvas | RAM | CPU | Qualité Image |
|----------|----------------|--------|-----|-----|---------------|
| **Actuel (640×480)** | ❌ 75% | 480×390 | 1.2 MB | Faible | Zoom 1.33x |
| **Solution 1 (640×368)** | ✅ 98% | **368**×390 | 0.9 MB | Faible | ⭐⭐⭐⭐⭐ |
| **Solution 2 (1920×1080)** | ✅ 100% | 480×390 | 6.2 MB | Élevé | ⭐⭐⭐⭐⭐ |
| **Solution 3 (640×368 upscale)** | ✅ 98% | 480×390 | 0.9 MB | Moyen | ⭐⭐⭐⭐ |

---

## 🎯 Recommandation

### Pour votre cas d'usage:

**Si vous pouvez adapter le canvas :**
→ **SOLUTION 1** (640×368 avec canvas 368×390)
- Meilleur compromis qualité/performance
- 98% du FOV
- Pas de upscale

**Si vous devez garder canvas 480×390 :**
→ **SOLUTION 3** (640×368 avec upscale)
- Toujours mieux que perdre 25% de FOV
- L'upscale est acceptable

**Si vous avez beaucoup de RAM/CPU :**
→ **SOLUTION 2** (1920×1080)
- FOV parfait (100%)
- Qualité maximale

---

## 📝 Migration Étape par Étape (Solution 1 - Recommandée)

### Étape 1: Modifier esp_cam_sensor

```yaml
esp_cam_sensor:
  resolution: "640x368"  # Changé de 640x480 → 640x368
  # Reste identique...
```

### Étape 2: Adapter le canvas

```yaml
- canvas:
    id: camera_canvas
    width: 368      # Changé de 480 → 368
    height: 390     # Inchangé
    x: 216          # Changé de 30 → 216 (centré)
    y: 40           # Inchangé
```

### Étape 3: Repositionner les boutons

Les boutons sont actuellement à `x: 690`. Avec le canvas plus étroit, vous avez plus d'espace:

```yaml
# OPTION A: Garder les boutons à droite
- button:
    x: 690  # Inchangé (plus d'espace entre canvas et boutons)

# OPTION B: Rapprocher les boutons
- button:
    x: 620  # Plus proche du canvas
```

### Étape 4: Tester

Vérifiez dans les logs:
```
✅ Using CUSTOM format: 640x368 RAW10 @ 30fps (near 16:9, ~2% crop, 16-byte aligned!)
```

L'image devrait maintenant montrer **98% de la scène** au lieu de 75% !

---

## 🔧 Exemple Complet (Solution 1)

```yaml
esp_cam_sensor:
  id: tab5_cam
  i2c_id: i2c_bus
  sensor_type: ov02c10
  resolution: "640x368"
  pixel_format: "RGB565"
  framerate: 30
  jpeg_quality: 15
  rotation: 270
  mirror_x: true
  mirror_y: false
  crop_offset_x: 0
  ppa_enabled: true

lvgl:
  pages:
    - id: camera_page
      bg_color: 0x1a1a1a
      on_load:
        - lambda: |-
            ESP_LOGI("camera", "Camera page loaded");
            static bool canvas_configured = false;
            if (!canvas_configured) {
              auto canvas = id(camera_canvas);
              if (canvas != nullptr) {
                id(camera_display).configure_canvas(canvas);
                canvas_configured = true;
                ESP_LOGI("lvgl", "✓ Canvas 368×390 configured (98% FOV!)");
              }
            }
      widgets:
        # Canvas adapté pour 640×368 avec rotation 270°
        - canvas:
            id: camera_canvas
            width: 368        # Réduit pour éviter upscale
            height: 390       # Garde hauteur
            x: 216            # Centré: (800-368)/2
            y: 40
            bg_color: 0x000000

        # Boutons (inchangés ou repositionnés)
        - button:
            id: camera_back
            width: 100
            height: 40
            x: 690  # Plus d'espace maintenant
            y: 10
            bg_color: 0xe74c3c
            on_click:
              then:
                - lambda: id(tab5_cam).stop_streaming();
                - lvgl.page.show: page_home
            widgets:
              - label:
                  text: "BACK"
                  text_color: 0xFFFFFF
                  text_font: nunito_24
```

---

## ❓ Pourquoi 640×368 "ne fonctionne pas" ?

Si vous avez l'erreur, c'est probablement parce que:

1. **Le PPA essaie de upscale 368→480** sans succès
   → Solution: Adapter le canvas à 368×390 (Solution 1)

2. **Format non reconnu**
   → Vérifiez les logs, le format existe dans le code

3. **Problème avec rotation + upscale**
   → Le PPA peut ne pas supporter upscale horizontal avec rotation
   → Solution: Utiliser canvas 368×390 (Solution 1)

---

## 🔗 Voir Aussi

- `OV02C10_640x480_ZOOM_FIX.md` - Détails sur le crop de 25%
- `ov02c10_custom_formats.h` - Liste des formats disponibles
- `OV02C10_NO_ZOOM_CONFIG.md` - Configuration sans zoom

---

## ✅ Résumé

**Le problème:** 640×480 crop 25% du FOV horizontal (zoom 1.33x)

**La solution:** Utilisez 640×368 (crop seulement 2%)

**Pour rotation 270° + canvas 480×390:**
- **Meilleur:** Canvas 368×390 (Solution 1)
- **Acceptable:** Upscale à 480×390 (Solution 3)
- **Si beaucoup de ressources:** 1920×1080 (Solution 2)
