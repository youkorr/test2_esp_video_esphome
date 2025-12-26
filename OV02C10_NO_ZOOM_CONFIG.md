# Configuration OV02C10 SANS ZOOM avec Rotation

## ✅ CORRECTION APPLIQUÉE (Commit 23f6e16)

**Tous les formats custom OV02C10 ont été corrigés** pour utiliser le full sensor (100% de la surface) au lieu du crop réduit (67%). Le zoom numérique ~1.5X a été **éliminé** de tous les formats.

## 🎯 Problème identifié (RÉSOLU)

Le sensor OV02C10 utilisait par défaut des formats custom (640x480, 800x600, 480x640) qui appliquaient un **crop (recadrage)** de 67% du sensor, créant un effet de zoom numérique ~1.5X qui empêchait la rotation de fonctionner correctement.

## ✅ Solution: Format 1920x1080 Full Sensor

### Comparaison AVANT/APRÈS la correction:

| Format | État | Crop Window | % Sensor | Zoom | Rotation |
|--------|------|-------------|----------|------|----------|
| **640x480** | ❌ AVANT | (320,180)→(1615,911) | 67% | ✅ 1.5X | ❌ Limitée |
| **640x480** | ✅ APRÈS | (0,4)→(1935,1091) | 100% | ❌ **AUCUN** | ✅ **Complète** |
| **800x600** | ❌ AVANT | (320,180)→(1615,911) | 67% | ✅ 1.5X | ❌ Limitée |
| **800x600** | ✅ APRÈS | (0,4)→(1935,1091) | 100% | ❌ **AUCUN** | ✅ **Complète** |
| **480x640** | ❌ AVANT | (320,180)→(1615,911) | 67% | ✅ 1.5X | ❌ Limitée |
| **480x640** | ✅ APRÈS | (0,4)→(1935,1091) | 100% | ❌ **AUCUN** | ✅ **Complète** |
| **1920x1080** | ✅ Toujours | (0,4)→(1935,1091) | 100% | ❌ **AUCUN** | ✅ **Complète** |

### Configuration YAML recommandée:

**BONNE NOUVELLE:** Tous les formats custom sont maintenant corrigés! Vous pouvez utiliser **n'importe quel format** sans zoom:

```yaml
# Configuration caméra OV02C10 - TOUS LES FORMATS SUPPORTENT LA ROTATION
esp_video:
  id: tab5_cam
  i2c_id: bsp_bus
  sensor_type: ov02c10
  sensor_addr: 0x36

  # ✅ TOUS CES FORMATS UTILISENT MAINTENANT LE FULL SENSOR (PAS DE ZOOM):
  resolution: "640x480"      # VGA - Full sensor, ISP downscale
  # resolution: "800x600"    # SVGA - Full sensor, ISP downscale
  # resolution: "480x640"    # Portrait - Full sensor, ISP downscale
  # resolution: "1920x1080"  # Full HD - Full sensor, pas de downscale

  pixel_format: RGB565
  framerate: 30

  # ✅ Rotation maintenant supportée sur TOUS les formats!
  rotate: 0                  # Ou 90, 180, 270 selon besoin

  # Miroir horizontal (si nécessaire)
  # mirror_x: true

lvgl_camera_display:
  id: camera_display
  camera_id: tab5_cam
  canvas_id: camera_canvas
  update_interval: 100ms
```

### Alternative: Format 1920x1080 avec 2 lanes MIPI (meilleure performance)

Si votre matériel supporte 2 lanes MIPI:

```yaml
esp_video:
  resolution: "1920x1080_2lane"  # Format 2-lane pour meilleure bande passante
```

## 🔍 Vérification des registres

### Format 1288x728 (AVEC zoom - à éviter):
```c
// Crop window: seulement 67% du sensor
{0x3800, 0x01},  {0x3801, 0x40},  // X start = 320
{0x3802, 0x00},  {0x3803, 0xb4},  // Y start = 180
{0x3804, 0x06},  {0x3805, 0x4f},  // X end = 1615
{0x3806, 0x03},  {0x3807, 0x8F},  // Y end = 911
// → Largeur crop: 1295px (67% du sensor) ❌ ZOOM ACTIF
```

### Format 1920x1080 (SANS zoom - recommandé):
```c
// Crop window: 100% du sensor
{0x3800, 0x00},  {0x3801, 0x00},  // X start = 0
{0x3802, 0x00},  {0x3803, 0x04},  // Y start = 4
{0x3804, 0x07},  {0x3805, 0x8f},  // X end = 1935
{0x3806, 0x04},  {0x3807, 0x43},  // Y end = 1091
// → Largeur crop: 1936px (100% du sensor) ✅ PAS DE ZOOM
```

## 📝 Note sur le fichier IPA (ov02c10_default.json)

**Le fichier IPA ne contrôle PAS le zoom!** Ce fichier contient uniquement:
- AWB (Auto White Balance)
- AGC (Auto Gain Control)
- CCM (Color Correction Matrix)
- ADN (Auto Denoise)
- AEN (Auto Enhancement - gamma, sharpen, contrast)

Le zoom est contrôlé par les **registres du sensor** (0x3800-0x3807), pas par le JSON IPA.

## 🎬 Migration étape par étape

1. **Sauvegardez** votre configuration actuelle
2. **Changez** `resolution: "800x480"` → `resolution: "1920x1080"`
3. **Ajoutez** le PPA pour redimensionner si besoin:
   ```yaml
   output_width: 800
   output_height: 480
   ```
4. **Testez** la rotation avec `rotate: 90` ou autres valeurs
5. **Ajustez** le `update_interval` si nécessaire (100-200ms recommandé)

## ⚠️ Points d'attention

1. Le format 1920x1080 nécessite **plus de bande passante MIPI**
   - Solution: utiliser 2 lanes MIPI si disponible
   - Ou: réduire le framerate à 20fps si problèmes

2. Le redimensionnement PPA consomme du **CPU/RAM**
   - Surveillez les watchdog timeouts
   - Augmentez `update_interval` si nécessaire

3. La rotation logicielle peut être **coûteuse**
   - Privilégiez la rotation hardware si disponible
   - Ou utilisez le format portrait natif (480x640)

## 📚 Formats disponibles (TOUS CORRIGÉS!)

Tous les formats OV02C10 supportés (source: `ov02c10.c:997-1106`):

**✅ TOUS les formats utilisent maintenant le FULL SENSOR (100% de la surface):**

- `640x480` - VGA, **SANS crop** ✅ Full sensor → ISP downscale
- `800x600` - SVGA, **SANS crop** ✅ Full sensor → ISP downscale
- `480x640` - Portrait, **SANS crop** ✅ Full sensor → ISP downscale
- `1920x1080` - Full HD, **SANS crop** ✅ Full sensor (pas de downscale)
- `1920x1080_2lane` - Full HD 2-lane, **SANS crop** ✅ **MEILLEURE PERFORMANCE**
- `1288x728` - Format natif legacy (toujours disponible mais non recommandé)

## 🔗 Références

- Fichier source: `components/esp_cam_sensor/sensor/ov02c10/ov02c10.c`
- Registres: `components/esp_cam_sensor/sensor/ov02c10/private_include/ov02c10_settings.h`
- IPA config: `components/esp_cam_sensor/sensor/ov02c10/cfg/ov02c10_default.json`
