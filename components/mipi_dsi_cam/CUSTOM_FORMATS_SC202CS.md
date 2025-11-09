# SC202CS Custom Format - VGA (640×480)

## Format Supporté

Le SC202CS supporte maintenant un format custom optimisé pour les petits écrans LCD :

- ✅ **VGA 640×480** @ 30fps RAW8

Ce format utilise RAW8 (comme les résolutions standard 720P) avec binning 2×2 et crop centré depuis la résolution native 1600×1200.

## Avantages du Format Custom VGA

### Pourquoi VGA pour SC202CS ?

1. **Résolution classique** : VGA est universellement supporté
2. **Binning optimal** : 2×2 depuis 1600×1200 donne une excellente qualité
3. **Faible bande passante** : 640×480 = 307,200 pixels @ 30fps
4. **MIPI 1-lane** : SC202CS utilise seulement 1 lane MIPI (économique)
5. **Parfait pour écrans 4-5"** : Taille optimale pour petits displays

### Comparaison avec 720P

| Paramètre | VGA Custom | 720P Standard |
|-----------|------------|---------------|
| Résolution | 640×480 | 1280×720 |
| Pixels | 307K | 922K |
| Mémoire buffer | ~600 KB | ~1.8 MB |
| Bande passante | ~9 MB/s | ~27 MB/s |
| Binning | 2×2 | Variable |
| Qualité | ★★★★☆ | ★★★★★ |

## Configuration ESPHome

### Pour écran VGA (640×480)

```yaml
mipi_dsi_cam:
  id: my_cam
  i2c_id: bsp_bus
  sensor_type: sc202cs
  resolution: "VGA"       # ou "640x480"
  pixel_format: "RGB565"
  framerate: 30

lvgl:
  pages:
    - widgets:
        - canvas:
            id: camera_canvas
            width: 640       # ✅ Résolution EXACTE de la caméra
            height: 480
```

### Alternative : Rester en 720P

Si vous préférez plus de détails, utilisez 720P :

```yaml
mipi_dsi_cam:
  sensor_type: sc202cs
  resolution: "720P"      # 1280×720 (résolution native standard)
  pixel_format: "RGB565"
```

**Note** : Avec 720P, vous devrez cropper ou scaler l'image si votre écran est plus petit.

## Comment ça fonctionne

### Pipeline de traitement

```
SC202CS Capteur (1600×1200)
         ↓
    Crop + Binning 2×2
         ↓
    RAW8 VGA (640×480)
         ↓
    ISP ESP32-P4 (Conversion)
         ↓
    RGB565 (640×480)
         ↓
    LVGL Canvas (640×480)
```

### Détection automatique

Le code détecte automatiquement si vous demandez VGA avec le SC202CS :

```cpp
if (this->sensor_name_ == "sc202cs") {
  if (width == 640 && height == 480) {
    // Applique automatiquement sc202cs_format_vga_raw8_30fps
    ESP_LOGI(TAG, "✅ Using CUSTOM format: VGA 640x480 RAW8 @ 30fps (SC202CS)");
  }
}
```

## Logs de démarrage

Quand le format custom est appliqué avec succès :

```
[mipi_dsi_cam] Detected sensor: sc202cs (PID: 0xEB52)
[mipi_dsi_cam] ✅ Using CUSTOM format: VGA 640x480 RAW8 @ 30fps (SC202CS)
[mipi_dsi_cam] ✅ Custom format applied successfully!
[mipi_dsi_cam]    Sensor registers configured for native VGA (640x480)
[mipi_dsi_cam] ✅ ISP Pipeline initialized
[mipi_dsi_cam]    Conversion: RAW8 (BGGR) → RGB565
[mipi_dsi_cam] ✅ Streaming started: 640×480 @ 30fps
```

## Détails Techniques

### Configuration VGA (640×480)

| Paramètre | Valeur |
|-----------|---------|
| Résolution sortie | 640×480 |
| Format pixel | RAW8 |
| Bayer pattern | BGGR |
| Framerate | 30 fps |
| Binning | 2×2 |
| Crop | Centré depuis 1600×1200 |
| MIPI lanes | 1 |
| MIPI clock | 72 MHz |
| HTS | 1500 pixels |
| VTS | 990 lignes |
| PCLK | ~28.8 MHz |
| Bande passante | ~9 MB/s |

### Crop Window

Le format VGA utilise un crop centré :
- **X start** : (1600 - 640×2) / 2 = 160
- **Y start** : (1200 - 480×2) / 2 = 120
- **X end** : 160 + 1280 - 1 = 1439
- **Y end** : 120 + 960 - 1 = 1079
- **Output** : 640×480 après binning 2×2

### Binning 2×2

Le binning 2×2 combine 4 pixels adjacents en 1 pixel :
- **Avantage** : Réduit le bruit, améliore sensibilité
- **Résolution effective** : 800×600 → 640×480 (avec crop)

## Comparaison avec autres capteurs

| Capteur | Format VGA Custom | Résolution Native | Bayer Pattern | MIPI Lanes |
|---------|-------------------|-------------------|---------------|------------|
| SC202CS | ✅ VGA RAW8 | 1600×1200 | BGGR | 1 |
| OV5647 | ✅ VGA RAW8 | 2592×1944 | GBRG | 2 |
| OV02C10 | ❌ (800×480 disponible) | 1920×1080 | BGGR | 2 |

## Optimisations Appliquées

### Binning et Crop

Le format VGA utilise :
- **Binning 2×2** : Améliore SNR (signal-to-noise ratio)
- **Crop centré** : Utilise le centre du capteur (meilleure qualité optique)
- **RAW8** : Format standard SC202CS (pas de conversion RAW10→RAW8)

### Configuration ISP

Le format custom inclut :
- **AEC/AGC** : Exposition et gain automatiques optimisés pour VGA
- **Timing optimisé** : HTS/VTS calculés pour 30fps stable
- **MIPI 1-lane** : Bande passante réduite (économie d'énergie)

## Dépannage

### Le format custom n'est pas appliqué

Si vous voyez :
```
[mipi_dsi_cam] ❌ VIDIOC_S_SENSOR_FMT failed
[mipi_dsi_cam] Custom format not supported, falling back to standard format
```

Vérifiez :
1. Capteur correctement détecté comme SC202CS (PID: 0xEB52)
2. ESP-IDF version 5.4.x ou supérieur
3. Composant esp_cam_sensor inclut le driver SC202CS

### Canvas LVGL a une taille de 0×0

```
[lvgl_camera_display] Taille canvas: 0x0  ← ❌ PROBLÈME
```

Solution : Définissez explicitement le canvas dans LVGL :
```yaml
lvgl:
  pages:
    - widgets:
        - canvas:
            id: camera_canvas
            width: 640    # Doit correspondre à la résolution caméra
            height: 480
```

### Image sombre ou surexposée

Les paramètres AEC/AGC sont optimisés pour conditions d'éclairage intérieur. Si besoin, vous pouvez ajuster l'exposition dans le code (registres 0x3e00-0x3e02).

## Consommation Mémoire

### VGA (640×480)

```
Buffer RGB565 : 640 × 480 × 2 = 614,400 bytes  (~600 KB)
Total PSRAM   : ~1 MB (avec buffers vidéo)
```

C'est très raisonnable pour ESP32-P4 avec 8 MB PSRAM.

## Test du Format

Pour tester si le format custom fonctionne :

1. **Vérifier les logs** : Cherchez "Using CUSTOM format: VGA 640x480"
2. **Vérifier l'image** : L'image doit être nette et bien exposée
3. **Vérifier le framerate** : Doit être stable à 30 fps

## Prochaines Étapes

Si vous avez besoin d'autres résolutions custom pour SC202CS :
- **800×480** : Pour écrans M5Stack et similaires
- **1024×600** : Pour écrans 7" (bien que OV5647 soit mieux adapté)
- **Autres** : Faites une demande sur GitHub

## Résumé Configuration

### Configuration Minimale

```yaml
network_init_guard:

esp_video:
  i2c_id: i2c_bus
  xclk_pin: GPIO36
  xclk_freq: 24000000
  enable_isp: true
  use_heap_allocator: true

mipi_dsi_cam:
  id: cam
  i2c_id: bsp_bus
  sensor_type: sc202cs     # ✅ SC202CS détecté
  resolution: "VGA"        # ✅ Format custom auto
  pixel_format: "RGB565"
  framerate: 30

lvgl:
  pages:
    - widgets:
        - canvas:
            id: camera_canvas
            width: 640       # ✅ Même résolution
            height: 480
```

## Références

- SC202CS Datasheet : Spécifications officielles SmartSens
- ESP32-P4 ISP : Documentation ESP-IDF sur l'Image Signal Processor
- Formats standard SC202CS : Voir `esp_cam_sensor/sensor/sc202cs/`
- Autres formats custom : `CUSTOM_FORMATS_OV02C10.md`, `CUSTOM_FORMATS_OV5647.md`
