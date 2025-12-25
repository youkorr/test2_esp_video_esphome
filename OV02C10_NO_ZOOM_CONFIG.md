# Configuration OV02C10 SANS ZOOM avec Rotation

## 🎯 Problème identifié

Le sensor OV02C10 utilise par défaut le format **1288x728** qui applique un **crop (recadrage)** de 67% du sensor, créant un effet de zoom numérique ~1.5X qui empêche la rotation de fonctionner correctement.

## ✅ Solution: Format 1920x1080 Full Sensor

### Comparaison des formats:

| Format | Crop Window | % Sensor | Zoom | Rotation |
|--------|-------------|----------|------|----------|
| **1288x728** (défaut) | (320,180)→(1615,911) | 67% | ✅ 1.5X | ❌ Limitée |
| **1920x1080** (full) | (0,4)→(1935,1091) | 100% | ❌ **AUCUN** | ✅ **Complète** |

### Configuration YAML recommandée:

```yaml
# Configuration caméra OV02C10 SANS zoom
esp_video:
  id: tab5_cam
  i2c_id: bsp_bus
  sensor_type: ov02c10
  sensor_addr: 0x36

  # ⭐ IMPORTANT: Utiliser le format FULL SENSOR (pas de zoom)
  resolution: "1920x1080"    # Format complet sans crop
  pixel_format: RGB565
  framerate: 30

  # PPA (Post-Processing Accelerator) pour redimensionner à votre écran
  # Décommentez si votre écran est 800x480:
  # output_width: 800
  # output_height: 480

  # Rotation supportée (0, 90, 180, 270)
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

## 📚 Formats disponibles

Tous les formats OV02C10 supportés (source: `ov02c10.c:997-1106`):

- `1288x728` - Défaut, **avec crop** ❌
- `640x480` - VGA, **avec crop** ❌
- `800x600` - SVGA, **avec crop** ❌
- `480x640` - Portrait, **avec crop** ❌
- `1920x1080` - Full HD, **SANS crop** ✅ **RECOMMANDÉ**
- `1920x1080_2lane` - Full HD 2-lane, **SANS crop** ✅ **MEILLEURE PERFORMANCE**

## 🔗 Références

- Fichier source: `components/esp_cam_sensor/sensor/ov02c10/ov02c10.c`
- Registres: `components/esp_cam_sensor/sensor/ov02c10/private_include/ov02c10_settings.h`
- IPA config: `components/esp_cam_sensor/sensor/ov02c10/cfg/ov02c10_default.json`
