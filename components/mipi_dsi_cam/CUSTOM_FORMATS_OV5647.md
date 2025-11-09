# OV5647 Custom Formats - Résolutions VGA et 1024×600

## Formats Supportés

L'OV5647 supporte maintenant des formats custom optimisés pour les petits écrans LCD :

- ✅ **VGA 640×480** @ 30fps RAW8
- ✅ **1024×600** @ 30fps RAW8

Ces formats utilisent RAW8 (au lieu de RAW10) car l'OV5647 produit une meilleure qualité en RAW8, et l'ISP ESP32-P4 peut convertir efficacement RAW8 → RGB565.

## Avantages des Formats Custom

### Pourquoi VGA (640×480) ?

1. **Résolution classique** : VGA est une résolution standard bien supportée
2. **Bonne qualité** : Utilise binning 4×4 depuis la résolution native 2592×1944
3. **Faible bande passante** : 640×480 = 307,200 pixels @ 30fps
4. **Compatible** : Fonctionne avec tous les écrans VGA et supérieurs

### Pourquoi 1024×600 ?

1. **Écrans 7"** : Résolution native des écrans LCD 7" courants
2. **Bon compromis** : Balance entre qualité et performance
3. **Binning 2×2** : Meilleure qualité que VGA grâce au binning plus faible
4. **M5Stack friendly** : Adapté aux écrans M5Stack et similaires

## Configuration ESPHome

### Pour écran VGA (640×480)

```yaml
mipi_dsi_cam:
  id: my_cam
  i2c_id: bsp_bus
  sensor_type: ov5647
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

### Pour écran 1024×600

```yaml
mipi_dsi_cam:
  id: my_cam
  i2c_id: bsp_bus
  sensor_type: ov5647
  resolution: "1024x600"  # Format custom automatique
  pixel_format: "RGB565"
  framerate: 30

lvgl:
  pages:
    - widgets:
        - canvas:
            id: camera_canvas
            width: 1024      # ✅ Résolution EXACTE de la caméra
            height: 600
```

## Comment ça fonctionne

### Pipeline de traitement

```
OV5647 Capteur (2592×1944)
         ↓
    Crop + Binning
         ↓
    RAW8 (VGA ou 1024×600)
         ↓
    ISP ESP32-P4 (Conversion)
         ↓
    RGB565 (même résolution)
         ↓
    LVGL Canvas
```

### Détection automatique

Le code détecte automatiquement si vous demandez VGA ou 1024×600 avec l'OV5647 :

```cpp
if (this->sensor_name_ == "ov5647") {
  if (width == 640 && height == 480) {
    // Applique automatiquement ov5647_format_640x480_raw8_30fps
  } else if (width == 1024 && height == 600) {
    // Applique automatiquement ov5647_format_1024x600_raw8_30fps
  }
}
```

## Logs de démarrage

Quand le format custom est appliqué avec succès :

```
[mipi_dsi_cam] ✅ Using CUSTOM format: VGA 640x480 RAW8 @ 30fps
[mipi_dsi_cam] ✅ Custom format applied successfully!
[mipi_dsi_cam]    Sensor registers configured for native 640x480
[mipi_dsi_cam] ✅ ISP Pipeline initialized
[mipi_dsi_cam]    Conversion: RAW8 (GBRG) → RGB565
```

ou pour 1024×600 :

```
[mipi_dsi_cam] ✅ Using CUSTOM format: 1024x600 RAW8 @ 30fps
[mipi_dsi_cam] ✅ Custom format applied successfully!
[mipi_dsi_cam]    Sensor registers configured for native 1024x600
```

## Détails Techniques

### Configuration VGA (640×480)

| Paramètre | Valeur |
|-----------|---------|
| Résolution sortie | 640×480 |
| Format pixel | RAW8 |
| Bayer pattern | GBRG |
| Framerate | 30 fps |
| Binning | 4×4 |
| HTS | 1896 pixels |
| VTS | 1080 lignes |
| PCLK | 32.4 MHz |
| MIPI clock | 192 MHz (4 lanes) |
| Bande passante | ~9.2 MB/s |

### Configuration 1024×600

| Paramètre | Valeur |
|-----------|---------|
| Résolution sortie | 1024×600 |
| Format pixel | RAW8 |
| Bayer pattern | GBRG |
| Framerate | 30 fps |
| Binning | 2×2 |
| HTS | 2416 pixels |
| VTS | 1300 lignes |
| PCLK | 94.2 MHz |
| MIPI clock | 288 MHz (4 lanes) |
| Bande passante | ~18.4 MB/s |

## Comparaison avec les résolutions standard

| Résolution | Format | Type | Qualité | Mémoire Buffer | Recommandation |
|------------|--------|------|---------|----------------|----------------|
| VGA 640×480 | RAW8 | Custom | ★★★★☆ | 300 KB | ✅ Écrans < 5" |
| 1024×600 | RAW8 | Custom | ★★★★★ | 600 KB | ✅ Écrans 7" |
| 800×800 | RAW8 | Standard | ★★★★☆ | 625 KB | Écrans carrés |
| 1080P | RAW8 | Standard | ★★★★★ | 2 MB | Écrans HD |

## Optimisations Appliquées

### Binning intelligent

- **VGA** : Binning 4×4 réduit le bruit et améliore la sensibilité
- **1024×600** : Binning 2×2 offre un meilleur compromis qualité/vitesse

### Configuration ISP

Les formats custom incluent :
- **AWB (Auto White Balance)** : Correction automatique de la balance des blancs
- **AEC/AGC** : Exposition et gain automatiques optimisés
- **LSC (Lens Shading Correction)** : Correction du vignettage
- **BLC (Black Level Calibration)** : Calibration du niveau de noir

### Crop centré

Les formats custom utilisent un crop centré depuis la résolution native OV5647 (2592×1944) :
- **VGA** : Utilise toute la largeur avec binning 4×4
- **1024×600** : Crop centré avec binning 2×2

## Dépannage

### Le format custom n'est pas appliqué

Si vous voyez :
```
[mipi_dsi_cam] ❌ VIDIOC_S_SENSOR_FMT failed
[mipi_dsi_cam] Custom format not supported, falling back to standard format
```

Vérifiez :
1. Capteur correctement détecté comme OV5647
2. ESP-IDF version 5.4.x ou supérieur
3. Composant esp_cam_sensor inclut les drivers OV5647

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

Les paramètres AEC/AGC peuvent être ajustés, mais les valeurs par défaut dans les formats custom sont optimisées pour la plupart des conditions d'éclairage intérieur.

## Consommation Mémoire

### VGA (640×480)

```
Buffer RGB565 : 640 × 480 × 2 = 614,400 bytes  (~600 KB)
Total PSRAM   : ~1 MB (avec buffers vidéo)
```

### 1024×600

```
Buffer RGB565 : 1024 × 600 × 2 = 1,228,800 bytes  (~1.2 MB)
Total PSRAM   : ~2 MB (avec buffers vidéo)
```

Les deux formats sont raisonnables pour ESP32-P4 avec 8 MB PSRAM.

## Test des Formats

Pour tester si le format custom fonctionne :

1. **Vérifier les logs** : Cherchez "Using CUSTOM format"
2. **Vérifier l'image** : L'image doit être nette et bien exposée
3. **Vérifier le framerate** : Doit être stable à 30 fps

## Prochaines Étapes

Si vous avez besoin d'autres résolutions custom pour OV5647 :
- **800×600** (SVGA)
- **1280×720** (720p)
- **1280×800** (WXGA)

Ouvrez une issue sur GitHub avec votre cas d'usage spécifique.

## Références

- OV5647 Datasheet : Spécifications officielles du capteur
- ESP32-P4 ISP : Documentation ESP-IDF sur l'Image Signal Processor
- Formats standard OV5647 : Voir `esp_cam_sensor/sensor/ov5647/`
