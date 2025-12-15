# OV02C10 Custom Formats - Résolutions natives 800x480 et 1280x800

## Vue d'ensemble

Ce composant supporte **des résolutions personnalisées non-standard** pour l'OV02C10 via configuration directe des registres du capteur :

- ✅ **1280x800** @ 30fps RAW10
- ✅ **800x480** @ 30fps RAW10

Ces résolutions sont configurées via `VIDIOC_S_SENSOR_FMT` (méthode utilisée par l'exemple `video_custom_format` d'ESP-IDF).

---

## Configuration YAML

### Pour écran 1280x800

```yaml
esp_video:
  i2c_id: bsp_bus
  xclk_pin: GPIO36
  xclk_freq: 24000000
  enable_isp: true       # ✅ ISP convertit RAW10 → RGB565
  enable_h264: true
  enable_jpeg: true

mipi_dsi_cam:
  id: tab5_cam
  sensor_type: ov02c10
  sensor_addr: 0x36
  pixel_format: "RGB565"  # Format de sortie final (après ISP)
  resolution: "1280x800"  # ✅ Résolution EXACTE de l'écran
  framerate: 30
```

### Pour écran 800x480

```yaml
mipi_dsi_cam:
  sensor_type: ov02c10
  sensor_addr: 0x36
  pixel_format: "RGB565"
  resolution: "800x480"   # ✅ Résolution EXACTE de l'écran
  framerate: 30
```

---

## Comment ça fonctionne

### Flux de traitement

```
OV02C10 capteur
    ↓
Configuration custom des registres (ov02c10_custom_formats.h)
    ↓
Sortie RAW10 @ résolution native (800x480 ou 1280x800)
    ↓
ISP ESP32-P4 (debayering + conversion)
    ↓
RGB565 @ résolution exacte
    ↓
Affichage LVGL (pixel-perfect, pas de redimensionnement)
```

### Détection automatique

Le code détecte automatiquement si vous demandez 800x480 ou 1280x800 avec l'OV02C10 :

```cpp
if (this->sensor_name_ == "ov02c10") {
  if (width == 1280 && height == 800) {
    // Applique automatiquement ov02c10_format_1280x800_raw10_30fps
  } else if (width == 800 && height == 480) {
    // Applique automatiquement ov02c10_format_800x480_raw10_30fps
  }
}
```

---

## Fichiers de configuration

### `ov02c10_custom_formats.h`

Contient les **configurations de registres** pour chaque résolution :

#### Structure de configuration

```c
typedef struct {
    uint16_t addr;  // Adresse registre OV02C10
    uint8_t val;    // Valeur à écrire
} ov02c10_reginfo_t;

static const ov02c10_reginfo_t ov02c10_1280x800_raw10_30fps[] = {
    {0x0103, 0x01},  // Software reset
    {0x0100, 0x00},  // Standby
    {0x3808, 0x05},  // H output size MSB (1280)
    {0x3809, 0x00},  // H output size LSB
    {0x380a, 0x03},  // V output size MSB (800)
    {0x380b, 0x20},  // V output size LSB
    // ... etc
};
```

#### Paramètres importants

| Paramètre | 1280x800 | 800x480 |
|-----------|----------|---------|
| Largeur | 1280px | 800px |
| Hauteur | 800px | 480px |
| Format | RAW10 Bayer | RAW10 Bayer |
| FPS | 30 | 30 |
| MIPI Clock | 400 MHz | 300 MHz |
| Lanes MIPI | 2 | 2 |
| XCLK | 24 MHz | 24 MHz |

---

## Logs de démarrage

Quand le format custom est appliqué avec succès :

```
[mipi_dsi_cam] ✅ Using CUSTOM format: 1280x800 RAW10 @ 30fps
[mipi_dsi_cam] ✅ Custom format applied successfully!
[mipi_dsi_cam]    Sensor registers configured for native 1280x800
```

Si le format custom échoue (fallback automatique) :

```
[mipi_dsi_cam] ❌ VIDIOC_S_SENSOR_FMT failed: Invalid argument
[mipi_dsi_cam] Custom format not supported, falling back to standard format
```

---

## Avantages vs Redimensionnement LVGL

### Avec Custom Format (cette implémentation)

- ✅ Résolution EXACTE de l'écran (pixel-perfect)
- ✅ Pas de redimensionnement = performance maximale
- ✅ Qualité optimale (pas de perte par scaling)
- ✅ Latence minimale
- ⚠️ Nécessite configuration registres correcte

### Avec Redimensionnement LVGL (fallback)

- ✅ Fonctionne immédiatement sans configuration
- ✅ Robuste (utilise résolutions standard)
- ⚠️ Redimensionnement CPU/GPU requis
- ⚠️ Légère perte de qualité si scaling important
- ⚠️ Latence supplémentaire (~5-10ms)

---

## Ajustement des registres (avancé)

⚠️ **Les configurations fournies sont des TEMPLATES** basés sur les formats standard OV02C10.

Si les résolutions ne fonctionnent pas parfaitement, vous devrez ajuster les registres selon le **datasheet OV02C10** :

### Registres clés à ajuster

| Registre | Description | Valeur 1280x800 | Valeur 800x480 |
|----------|-------------|----------------|----------------|
| 0x3808-09 | H output size | 0x0500 (1280) | 0x0320 (800) |
| 0x380a-0b | V output size | 0x0320 (800) | 0x01E0 (480) |
| 0x380c-0d | HTS (total H) | 0x05DC (1500) | 0x041A (1050) |
| 0x380e-0f | VTS (total V) | 0x0352 (850) | 0x020E (526) |
| 0x3810-11 | H offset | 0x0140 (320) | 0x0230 (560) |
| 0x3812-13 | V offset | 0x008C (140) | 0x012C (300) |
| 0x4837 | MIPI timing | 0x14 | 0x1C |

### Calcul des offsets (crop depuis 1920x1080)

**Pour 1280x800** :
- H offset = (1920 - 1280) / 2 = 320 pixels
- V offset = (1080 - 800) / 2 = 140 lignes

**Pour 800x480** :
- H offset = (1920 - 800) / 2 = 560 pixels
- V offset = (1080 - 480) / 2 = 300 lignes

---

## Référence ESP-IDF

Cette implémentation est basée sur :
- [ESP-IDF video_custom_format example](https://github.com/espressif/esp-video/tree/main/examples/video_custom_format)
- Méthode `VIDIOC_S_SENSOR_FMT` (commande V4L2 personnalisée ESP-IDF)

---

## Dépannage

### Problème : Écran noir

**Cause** : Registres de timing incorrects

**Solution** : Ajuster HTS/VTS et MIPI clock dans `ov02c10_custom_formats.h`

### Problème : Image décalée/rognée

**Cause** : Offsets H/V incorrects

**Solution** : Ajuster registres 0x3810-0x3813 (crop window)

### Problème : Artefacts/bandes

**Cause** : MIPI clock trop rapide

**Solution** : Réduire `.mipi_info.mipi_clk` (essayer 300MHz au lieu de 400MHz)

### Problème : FPS instable

**Cause** : VTS trop petit (vertical blanking insuffisant)

**Solution** : Augmenter VTS (registres 0x380e-0f)

---

## Tests recommandés

1. **Tester avec 1920x1080 standard d'abord** (vérifier capteur fonctionne)
2. **Essayer 1280x800** (vérifier logs pour "Custom format applied successfully")
3. **Essayer 800x480** (vérifier résolution exacte sur écran)
4. **Mesurer FPS réels** (devrait être 30fps stable)
5. **Vérifier qualité image** (pas d'artefacts, couleurs correctes)

---

## Performance attendue

| Résolution | FPS | Bande passante | RAM |
|-----------|-----|----------------|-----|
| 1280x800 | 30 | ~60 MB/s | 2 MB |
| 800x480 | 30 | ~23 MB/s | 750 KB |

**Note** : Ces valeurs sont pour RGB565 après conversion ISP.

---

## Contribution

Si vous ajustez les registres pour améliorer la qualité, merci de partager vos configurations !

Les registres finaux devront être validés avec :
- Oscilloscope MIPI
- Datasheet OV02C10 officiel (Omnivision)
- Tests matériels sur ESP32-P4
