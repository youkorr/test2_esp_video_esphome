# Architecture du Pipeline Vidéo ESP32-P4

## Pipeline Complet

```
┌─────────────────┐
│ Capteur MIPI-CSI│  RAW8/RAW10
│  (OV5647, etc)  │
└────────┬────────┘
         │
         ├─────────> /dev/video0 (CSI device)
         │
         v
┌─────────────────┐
│  ISP Processor  │  RAW → RGB565
│   /dev/video20  │  + IPA algorithms:
│                 │    - AWB (Auto White Balance)
│                 │    - AE (Auto Exposure)
│                 │    - Sharpen
│                 │    - Color correction
└────────┬────────┘
         │
         ├─────────> RGB565 direct
         │
         ├─────────> /dev/video10 (JPEG encoder)
         │            RGB565 → JPEG
         │
         └─────────> /dev/video11 (H264 encoder)
                      RGB565 → H264
```

## ✅ Configuration Automatique par esp_video

### 1. ISP Pipeline (esp_video_isp_pipeline)

**Initialisé automatiquement** par `esp_video_component.cpp` quand `enable_isp: true`

```cpp
esp_video_isp_pipeline_init(&isp_config);
```

**Ce pipeline gère automatiquement:**
- ✅ Conversion RAW → RGB565
- ✅ Auto White Balance (AWB) via IPA
- ✅ Auto Exposure (AE) via IPA
- ✅ Sharpen via IPA
- ✅ Color correction (brightness, contrast, saturation)
- ✅ Histogram
- ✅ Demosaicing (Bayer → RGB)

### 2. IPA (Image Processing Algorithms)

**Configuration JSON embarquée** pour chaque capteur:
- `ov5647_ipa_config_json` → OV5647
- `ov02c10_ipa_config_json` → OV02C10
- `sc202cs_ipa_config_json` → SC202CS (si disponible)

Ces configs JSON contiennent les paramètres optimaux pour chaque capteur.

### 3. Valeurs ISP par Défaut

Dans `esp_video_isp_device.c`:
```c
#define ISP_BRIGHTNESS_DEFAULT      0
#define ISP_CONTRAST_DEFAULT        128
#define ISP_SATURATION_DEFAULT      128
#define ISP_HUE_DEFAULT             0
```

Ces valeurs sont **bonnes pour la plupart des cas**.

## ❌ Pourquoi mes Tentatives de Configuration Manuelle Échouaient

J'essayais de configurer l'ISP via V4L2 controls sur `/dev/video20`:

```cpp
// ❌ CECI NE FONCTIONNE PAS - errno=22
ctrl.id = V4L2_CID_BRIGHTNESS;
ctrl.value = 60;
ioctl(isp_fd, VIDIOC_S_CTRL, &ctrl);  // Échec!
```

**Pourquoi ça échoue:**
- Les contrôles V4L2 standards (BRIGHTNESS, CONTRAST, SATURATION) ne sont pas exposés par esp_video
- esp_video utilise son API interne pour configurer l'ISP
- Le pipeline IPA gère dynamiquement ces paramètres

## ✅ Solution Correcte

**NE RIEN FAIRE!** Le pipeline ISP est déjà configuré et actif:

```
[18:06:18][I][esp_video:365]: ✅ ISP Pipeline active - IPA algorithms running
```

Cela signifie:
- ✅ ISP fonctionne
- ✅ AWB (Auto White Balance) actif
- ✅ AE (Auto Exposure) actif
- ✅ Sharpen actif
- ✅ Color correction active

## Problèmes Restants

### 1. Chip ID = 0x0000 (CRITIQUE)

```
✅ I2C lecture réussie: Chip ID = 0x0000 (attendu: 0xEB52)
❌ ID invalide - XCLK probablement inactif
```

**Cause:** Le capteur ne reçoit pas XCLK (horloge 24MHz)

**Solution:** Vérifier dans `esp_video_init.c` que:
1. LDO 2.5V est initialisé AVANT XCLK
2. XCLK est configuré avec `xclk_freq_hz = 24000000`
3. Délai de 20ms après XCLK avant accès I2C

### 2. OV5647 Couleur Rouge

**Correction appliquée:** `OV5647_AE_TARGET_DEFAULT = 0x36`

Cette valeur devrait corriger le problème MAIS elle ne sera effective que si:
- Le capteur fonctionne (Chip ID correct, pas 0x0000)
- Le pipeline IPA a convergé (2-3 secondes après démarrage)

**Test:** Attendre 5 secondes après le démarrage et capturer une image pour voir si l'AE a convergé.

### 3. Reboot/Crash

**Besoin de plus d'informations:**
- À quel moment exact?
- Message de panic?
- Quelle caméra?

## Configuration YAML Recommandée

```yaml
esp_video:
  enable_isp: true      # ✅ Active l'ISP Pipeline + IPA
  enable_jpeg: true     # ✅ Active encodeur JPEG
  enable_h264: true     # ✅ Active encodeur H264

mipi_dsi_cam:
  sensor: "ov5647"
  resolution: "1024x600"
  pixel_format: "RGB565"  # ⚠️ Pas "RB565"!
  framerate: 30
```

## Résumé

**Ce que je faisais avant (❌ INCORRECT):**
- Essayer de configurer ISP manuellement via V4L2
- Ajouter des fonctions `isp_apply_color_correction_()`
- Essayer d'activer AWB manuellement

**Ce qui se passe réellement (✅ CORRECT):**
- esp_video configure tout automatiquement
- ISP Pipeline + IPA gèrent dynamiquement la qualité d'image
- Les valeurs sont optimales par défaut

**Corrections qui restent valides:**
- ✅ OV5647 AE_TARGET = 0x36 (dans le driver du capteur)
- ✅ PPA API corrigée pour ESP-IDF 5.3+
- ✅ Suppression du code ISP manuel qui échouait

**Problèmes à résoudre:**
- 🔴 Chip ID 0x0000 → XCLK non configuré
- 🔴 Reboot → Besoin logs complets
