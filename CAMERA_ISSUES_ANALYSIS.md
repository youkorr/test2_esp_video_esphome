# Analyse des problèmes de caméra

## Problèmes identifiés dans les logs

### 1. ❌ Chip ID = 0x0000 (SC202CS)

**Log:**
```
✅ /dev/video0 existe et accessible (CSI video device - capteur détecté!)
✅ I2C lecture réussie: Chip ID = 0x0000 (attendu: 0xEB52 pour SC202CS)
❌ ID invalide - XCLK probablement inactif ou capteur déconnecté
```

**Cause probable:** XCLK (horloge externe du capteur) non configuré ou inactif

**Solution:**
Le composant `esp_video` doit initialiser XCLK avant la détection du capteur. Le problème vient probablement de l'ordre d'initialisation dans `esp_video_component.cpp`.

Vérifier que:
1. XCLK est configuré avec `xclk_freq_hz` correct (typiquement 24 MHz)
2. Le capteur reçoit bien l'horloge avant l'accès I2C
3. Le LDO (2.5V) est bien initialisé avant XCLK

### 2. ⚠️ Erreurs ISP V4L2 Controls (CORRIGÉ)

**Log:**
```
[E][mipi_dsi_cam:073]: ioctl(VIDIOC_S_CTRL(BRIGHTNESS)) a échoué: errno=22 (Invalid argument)
[E][mipi_dsi_cam:073]: ioctl(VIDIOC_S_CTRL(CONTRAST)) a échoué: errno=22 (Invalid argument)
[E][mipi_dsi_cam:073]: ioctl(VIDIOC_S_CTRL(SATURATION)) a échoué: errno=22 (Invalid argument)
```

**Cause:** Contrôles V4L2 standard non exposés par esp_video sur /dev/video20

**Solution appliquée:**
- Supprimé les tentatives de configuration ISP via V4L2
- esp_video configure déjà l'ISP avec les valeurs par défaut:
  - brightness: 0
  - contrast: 128
  - saturation: 128

Ces valeurs par défaut sont acceptables pour la plupart des cas d'usage.

### 3. ✅ OV5647 couleur rouge (CORRIGÉ)

**Correction appliquée:**
- `OV5647_AE_TARGET_DEFAULT`: 0x30 (48) → 0x36 (54)
- Fichier: `components/esp_cam_sensor/sensor/ov5647/ov5647.c:26`

Cela devrait corriger le problème de teinte rouge et de bruit.

### 4. ⚠️ Format "RB565" au lieu de "RGB565"

**Log:**
```
Camera ready: RB565 @ 1024x600 (30 fps)
```

**Cause:** Erreur de configuration dans le fichier YAML

**Solution:**
Vérifier le fichier de configuration ESPHome et corriger:
```yaml
mipi_dsi_cam:
  pixel_format: "RGB565"  # Pas "RB565"
```

## Corrections appliquées

### Commit a055e47: Remove non-functional ISP V4L2 controls
- Supprimé `isp_apply_color_correction_()`
- Supprimé `sensor_enable_auto_white_balance_()`
- Élimine les erreurs errno=22

### Commit 7e3284c: Add ISP color correction and fix OV5647 red tint issue
- Corrigé OV5647 AE_TARGET: 0x30 → 0x36
- (Les contrôles ISP V4L2 ont été supprimés dans commit suivant)

### Commit c6cfacf: Fix PPA API to match M5Stack Tab5 implementation
- Corrigé l'API PPA pour ESP-IDF 5.3+
- Utilise `ppa_do_scale_rotate_mirror(handle, &config)` (2 paramètres)

### Commit 84ae088: Update ESP-IDF version compatibility to 5.3+
- Support ESP-IDF 5.3 et supérieur
- Ajouté `idf_component.yml`

### Commit 40140ff: Fix PPA header include path
- Corrigé: `esp_ppa.h` → `driver/ppa.h`

## Problèmes restants

### 🔴 Reboot/crash des caméras

**Informations nécessaires:**
1. À quel moment le reboot se produit-il?
   - Au démarrage de l'ESP32?
   - Lors de l'initialisation du capteur?
   - Lors de la première capture?
   - Après quelques frames?

2. Y a-t-il un message de panic ou de crash avant le reboot?
   - Regarder les logs série complets
   - Chercher "Guru Meditation Error" ou "LoadProhibited"

3. Quelle caméra est testée?
   - OV5647?
   - SC202CS?
   - OV02C10?

### 🔴 Chip ID 0x0000 (SC202CS)

**Solution possible:**
Modifier `esp_video_component.cpp` pour s'assurer que:
1. LDO est initialisé en premier (2.5V pour MIPI)
2. XCLK est configuré et activé avant la détection I2C
3. Délai suffisant après XCLK avant lecture I2C (10-20ms)

**Ordre d'initialisation correct:**
```
1. GPIO reset/pwdn
2. LDO 2.5V (esp_ldo_acquire_channel)
3. XCLK 24MHz
4. Delay 20ms
5. Reset sequence
6. I2C detection
```

## Configuration recommandée

### Pour OV5647 (non-M5Stack)
```yaml
mipi_dsi_cam:
  sensor: "ov5647"
  resolution: "1024x600"  # ou "VGA" 640x480
  pixel_format: "RGB565"
  framerate: 30
```

### Pour SC202CS (M5Stack uniquement)
```yaml
mipi_dsi_cam:
  sensor: "sc202cs"
  resolution: "VGA"
  pixel_format: "RGB565"
  framerate: 30
```

### Pour OV02C10 (non-M5Stack)
```yaml
mipi_dsi_cam:
  sensor: "ov02c10"
  resolution: "800x480"  # ou "1280x800"
  pixel_format: "RGB565"
  framerate: 30
```

## Notes importantes

1. **M5Stack Tab5 vs autres ESP32-P4:**
   - M5Stack Tab5 utilise SC202CS
   - Autres ESP32-P4 utilisent OV5647 ou OV02C10
   - Les configurations ISP peuvent différer selon le board

2. **ISP color correction:**
   - esp_video configure automatiquement l'ISP
   - Les valeurs par défaut sont adaptées pour la plupart des cas
   - Modification nécessite de modifier esp_video directement (pas via V4L2)

3. **PPA (Pixel-Processing Accelerator):**
   - Activé uniquement si mirror_x, mirror_y, ou rotation configurés
   - Utilise hardware DMA (zéro CPU)
   - Compatible ESP-IDF 5.3+
