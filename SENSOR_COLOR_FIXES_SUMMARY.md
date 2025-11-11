# Résumé des Corrections: Capteurs Sans Bruit et Couleurs Correctes

## ✅ Priorité: Faire Fonctionner les Capteurs

### Problème Principal: Chip ID = 0x0000

**Symptôme:** Les capteurs ne sont pas détectés
```
❌ ID invalide - XCLK probablement inactif ou capteur déconnecté
```

**Cause:** XCLK (horloge externe 24MHz) n'est pas initialisé pour les boards non-M5Stack

**✅ Solution Appliquée:**
- Ajouté paramètre `enable_xclk_init` dans esp_video
- Initialise XCLK via LEDC avant la détection du capteur
- Délai de stabilisation 50ms après XCLK

**Configuration Requise (Boards non-M5Stack):**
```yaml
esp_video:
  enable_xclk_init: true  # ⭐ OBLIGATOIRE pour OV5647, OV02C10
  xclk_pin: GPIO36
  xclk_freq: 24000000
```

---

## ✅ OV5647: Correction Image Rouge et Bruitée

### Symptômes
- Image dominée par la couleur rouge
- Image bruitée/granuleuse

### Cause
`OV5647_AE_TARGET_DEFAULT = 0x50` (80) → Trop élevé
- Forçait un gain élevé → bruit
- Mauvais équilibre des couleurs → rouge dominant

### ✅ Correction Appliquée
**Fichier:** `components/esp_cam_sensor/sensor/ov5647/ov5647.c:26`

```c
// AVANT
#define OV5647_AE_TARGET_DEFAULT (0x50)  // 80

// APRÈS
#define OV5647_AE_TARGET_DEFAULT (0x36)  // 54 (valeur optimale M5Stack)
```

### Résultats Attendus
- ✅ Réduction du bruit (gain plus faible)
- ✅ Meilleur équilibre des couleurs
- ✅ Moins de dominance rouge

---

## ✅ SC202CS: Correction Image Verte

### Symptômes
- Eclairage fort et couleur verte dominante
- Image sursaturée

### Causes
1. `gain_def = 0` → Pas de gain analogique, ISP surcompense
2. `exp_def = 0x4dc` (1244) → 99% de l'exposition max, surexposition
3. `DIG_GAIN_PRIORITY` → Gain numérique prioritaire (plus de bruit)

### ✅ Corrections Appliquées

**Fichier:** `components/esp_cam_sensor/sensor/sc202cs/sc202cs.c`

#### 1. Gain et Exposition (lignes 882-904)
```c
// AVANT
.gain_def = 0,          // Pas de gain!
.exp_def = 0x4dc,      // 99% expo max

// APRÈS
.gain_def = 32,         // 2x gain analogique
.exp_def = 0x300,      // 60% expo max (768)
```

**Appliqué aux 4 formats:** VGA, 800x640, 1024x600, 1280x800

#### 2. Priorité Gain Analogique (Kconfig.sc202cs:63)
```
// AVANT
default CAMERA_SC202CS_DIG_GAIN_PRIORITY

// APRÈS
default CAMERA_SC202CS_ANA_GAIN_PRIORITY
```

### Résultats Attendus
- ✅ Réduction teinte verte
- ✅ Meilleure exposition (pas sursaturé)
- ✅ Moins de bruit (gain analogique)

---

## Configuration Complète par Capteur

### OV5647 (Non-M5Stack)
```yaml
esp_video:
  i2c_id: i2c_bus
  enable_isp: true
  enable_jpeg: true
  enable_xclk_init: true     # ⭐ OBLIGATOIRE
  xclk_pin: GPIO36
  xclk_freq: 24000000

mipi_dsi_cam:
  sensor: "ov5647"
  resolution: "1024x600"
  pixel_format: "RGB565"     # Pas "RB565"!
  framerate: 30
```

**Corrections actives:**
- ✅ AE_TARGET = 0x36 (corrige rouge + bruit)
- ✅ XCLK initialisé (Chip ID correct)

### SC202CS (M5Stack Tab5)
```yaml
esp_video:
  i2c_id: i2c_bus
  enable_isp: true
  enable_jpeg: true
  enable_xclk_init: false    # ⭐ BSP initialise déjà

mipi_dsi_cam:
  sensor: "sc202cs"
  resolution: "VGA"
  pixel_format: "RGB565"
  framerate: 30
```

**Corrections actives:**
- ✅ gain_def = 32 (corrige vert)
- ✅ exp_def = 0x300 (corrige surexposition)
- ✅ ANA_GAIN_PRIORITY (moins de bruit)

### OV02C10 (Non-M5Stack)
```yaml
esp_video:
  i2c_id: i2c_bus
  enable_isp: true
  enable_jpeg: true
  enable_xclk_init: true     # ⭐ OBLIGATOIRE
  xclk_pin: GPIO36
  xclk_freq: 24000000

mipi_dsi_cam:
  sensor: "ov02c10"
  resolution: "800x480"
  pixel_format: "RGB565"
  framerate: 30
```

**Corrections actives:**
- ✅ XCLK initialisé (Chip ID correct)
- ✅ ANA_GAIN_PRIORITY (moins de bruit)

---

## Pipeline ISP Automatique

**IMPORTANT:** L'ISP et les algorithmes IPA sont configurés automatiquement!

```
[I][esp_video:365]: ✅ ISP Pipeline active - IPA algorithms running
```

Cela signifie que ces fonctions sont DÉJÀ actives:
- ✅ Auto White Balance (AWB)
- ✅ Auto Exposure (AE)
- ✅ Sharpen
- ✅ Color correction (brightness, contrast, saturation)
- ✅ Demosaicing (Bayer → RGB)

**Vous n'avez PAS besoin de configurer l'ISP manuellement!**

---

## Commits Appliqués

1. **13d51f2:** Add enable_xclk_init parameter for non-M5Stack boards
   - Résout Chip ID = 0x0000

2. **a055e47:** Remove non-functional ISP V4L2 controls
   - Supprime code qui causait errno=22

3. **7e3284c:** Add ISP color correction and fix OV5647 red tint issue
   - OV5647 AE_TARGET = 0x36

4. **c6cfacf:** Fix PPA API to match M5Stack Tab5 implementation
   - Corrige API PPA pour ESP-IDF 5.3+

5. **84ae088:** Update ESP-IDF version compatibility to 5.3+
   - Support ESP-IDF 5.3 et supérieur

---

## Vérification

### Logs de Succès Attendus

```
🔧 Initializing XCLK for non-M5Stack board (GPIO36 @ 24000000 Hz)
✅ XCLK initialized successfully via LEDC
✅ /dev/video0 existe et accessible (CSI video device - capteur détecté!)
✅ I2C lecture réussie: Chip ID = 0x5647 (OV5647) ✓  [ou 0xEB52 pour SC202CS]
✅ ISP Pipeline active - IPA algorithms running
✅ esp-cam-sensor: ok (ov5647)
Camera ready: RGB565 @ 1024x600 (30 fps)
```

### Si Chip ID = 0x0000 Persiste

1. Vérifiez `enable_xclk_init: true` (boards non-M5Stack)
2. Vérifiez le GPIO dans votre schéma
3. Essayez un autre GPIO: `xclk_pin: GPIO15`

### Si Crash/Reboot

1. M5Stack? → Désactivez `enable_xclk_init: false`
2. Conflit GPIO? → Changez `xclk_pin`
3. Réduisez fréquence → `xclk_freq: 20000000`

---

## Résumé

**Pour avoir des capteurs qui fonctionnent sans bruit et avec des couleurs correctes:**

1. ✅ **Activez XCLK** (boards non-M5Stack): `enable_xclk_init: true`
2. ✅ **OV5647**: AE_TARGET = 0x36 (déjà appliqué)
3. ✅ **SC202CS**: gain=32, exp=0x300, ANA_GAIN (déjà appliqué)
4. ✅ **ISP Pipeline**: Configuré automatiquement (AWB, AE, etc.)

**Les corrections de couleur sont déjà dans le code. Il suffit maintenant d'activer XCLK pour que les capteurs soient détectés!**
