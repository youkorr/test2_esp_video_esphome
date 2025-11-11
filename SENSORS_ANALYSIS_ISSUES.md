# ANALYSE COMPLÈTE DES SENSORS MIPI-CSI

Date: 2025-11-10

---

## ✅ 1. VÉRIFICATION: Tous les sensors utilisent esp_sccb_intf

### OV02C10 (ov02c10.c)
```c
// Ligne 1053-1061
static esp_err_t ov02c10_read(esp_sccb_io_handle_t sccb_handle, uint16_t reg, uint8_t *read_buf)
{
    return esp_sccb_transmit_receive_reg_a16v8(sccb_handle, reg, read_buf);  ✅
}

static esp_err_t ov02c10_write(esp_sccb_io_handle_t sccb_handle, uint16_t reg, uint8_t data)
{
    return esp_sccb_transmit_reg_a16v8(sccb_handle, reg, data);  ✅
}
```

### SC202CS (sc202cs.c)
```c
// Ligne 1001-1009
static esp_err_t sc202cs_read(esp_sccb_io_handle_t sccb_handle, uint16_t reg, uint8_t *read_buf)
{
    return esp_sccb_transmit_receive_reg_a16v8(sccb_handle, reg, read_buf);  ✅
}

static esp_err_t sc202cs_write(esp_sccb_io_handle_t sccb_handle, uint16_t reg, uint8_t data)
{
    return esp_sccb_transmit_reg_a16v8(sccb_handle, reg, data);  ✅
}
```

### OV5647 (ov5647.c)
```c
// Ligne 177-185
static esp_err_t ov5647_read(esp_sccb_io_handle_t sccb_handle, uint16_t reg, uint8_t *read_buf)
{
    return esp_sccb_transmit_receive_reg_a16v8(sccb_handle, reg, read_buf);  ✅
}

static esp_err_t ov5647_write(esp_sccb_io_handle_t sccb_handle, uint16_t reg, uint8_t data)
{
    return esp_sccb_transmit_reg_a16v8(sccb_handle, reg, data);  ✅
}
```

**✅ CONCLUSION: Tous les 3 sensors utilisent correctement esp_sccb_intf (esp_sccb_transmit_*)**

---

## ✅ 2. CORRECTION: OV02C10 supporte 1-lane ET 2-lane

### Formats disponibles (ov02c10.c ligne 996-1051):

| Index | Nom | Résolution | FPS | Lanes | Format |
|-------|-----|------------|-----|-------|--------|
| 0 | MIPI_1lane_24Minput_RAW10_1288x728_30fps | 1288×728 | 30 | **1** | RAW10 |
| 1 | MIPI_1lane_24Minput_RAW10_1920x1080_30fps | 1920×1080 | 30 | **1** | RAW10 |
| 2 | MIPI_**2**lane_24Minput_RAW10_1920x1080_30fps | 1920×1080 | 30 | **2** | RAW10 |

**L'utilisateur a raison**: Le format par défaut (index 0 ou 1) utilise **1-lane**, pas 2-lane.

**Configuration pour 1-lane (recommandé)**:
```yaml
mipi_dsi_cam:
  id: tab5_cam
  sensor_type: ov02c10
  resolution: "1920x1080"  # Format index 1 (1-lane)
  # OU
  # resolution: "1288x728"  # Format index 0 (1-lane)
```

---

## 🔴 3. PROBLÈME: SC202CS - Éclairage fort et vert

### Symptômes rapportés:
- **Éclairage trop fort** (surexposition)
- **Teinte verte** dominante

### Analyse du code SC202CS:

#### A. Gain par défaut = 0 (aucun gain!)

```c
// sc202cs.c ligne 882-884, 893-895, 904-906, 915-917
.gain_def = 0,  // ❌ gain index = 0
```

**Problème**: `gain_def = 0` signifie **aucun gain analogique ni digital**!

Regardons le gain_map index 0:
```c
// sc202cs.c ligne 273 (ANA_GAIN_PRIORITY) ou ligne 675 (DIG_GAIN_PRIORITY)
{0x80, 0x00, 0x00},  // Index 0: dgain_fine=0x80, dgain_coarse=0x00, analog_gain=0x00
```

- **Analog gain = 0x00** = gain minimal
- **Digital coarse = 0x00** = pas de gain digital coarse
- **Digital fine = 0x80** = gain digital fine minimal (128/256 = 0.5x)

**Résultat**: Le sensor a un gain **trop faible**, ce qui peut forcer l'ISP à **suramplifier** les couleurs, créant la teinte verte.

#### B. Exposition par défaut = 0x4dc (élevée)

```c
// sc202cs.c ligne 884
.exp_def = 0x4dc,  // = 1244 en décimal
```

**Calcul**:
- VTS = 1250 (ligne 880)
- Exposition max = VTS - 6 = 1244 ✅ (ligne 1111-1113)
- Exposition par défaut = 1244 = **99.5% du maximum!**

**Problème**: L'exposition par défaut est quasiment au maximum, ce qui provoque **surexposition** (image trop lumineuse).

#### C. Bayer pattern = BGGR (correct pour SC202CS)

```c
// sc202cs.c ligne 885
.bayer_type = ESP_CAM_SENSOR_BAYER_BGGR,  ✅ Correct pour SC202CS
```

Le Bayer pattern est correct.

### 🎯 SOLUTIONS pour SC202CS:

#### Solution 1: Réduire l'exposition par défaut (Recommandé)

**Modifier sc202cs.c ligne 884, 895, 906, 917**:
```c
.exp_def = 0x300,  // ← Changer de 0x4dc à 0x300 (environ 60% au lieu de 99%)
```

**Calcul**: 0x300 = 768 décimal = 768/1244 = **61% de l'exposition max**

#### Solution 2: Augmenter le gain par défaut

**Modifier sc202cs.c ligne 882-883, 893-894, 904-905, 915-916**:
```c
.gain_def = 32,  // ← Changer de 0 à 32 (gain 2x analogique)
```

Index 32 dans le gain_map correspond à:
```c
// sc202cs_gain_map[32] pour ANA_GAIN_PRIORITY
{0x80, 0x00, 0x01},  // analog_gain = 0x01 = 2x gain
```

#### Solution 3: Ajuster via YAML (sans modifier le code)

```yaml
mipi_dsi_cam:
  id: tab5_cam
  sensor_type: sc202cs
  resolution: "1600x1200"
  pixel_format: RGB565
  framerate: 30

  # Ajouter des paramètres de contrôle
  exposure: 768      # ← Réduire exposition (au lieu de 1244)
  gain: 32           # ← Augmenter gain (au lieu de 0)
```

---

## 🔴 4. PROBLÈME: OV5647 - Couleur rouge et bruitée

### Symptômes rapportés:
- **Couleur rouge** dominante
- **Image bruitée** (grain/artifacts)

### Analyse du code OV5647:

#### A. Pas de contrôle de gain/exposition moderne

OV5647 utilise un **ancien système de contrôle AE (Auto-Exposure)**:

```c
// ov5647.c ligne 313-336
static esp_err_t ov5647_set_AE_target(esp_cam_sensor_device_t *dev, int target)
{
    // ...
    int AE_low = target * 23 / 25;  /* 0.92 */
    int AE_high = target * 27 / 25; /* 1.08 */
    // ...
}
```

**Problème**: OV5647 n'implémente **PAS** de contrôle direct de gain comme OV02C10 ou SC202CS!

Regardons les fonctions supportées:
```c
// ov5647.c ligne 367-370
static esp_err_t ov5647_get_para_value(esp_cam_sensor_device_t *dev, uint32_t id, void *arg, size_t size)
{
    return ESP_ERR_NOT_SUPPORTED;  // ❌ Pas de get gain/exposure!
}
```

**Seul paramètre supporté**:
```c
// ov5647.c ligne 351-357
case ESP_CAM_SENSOR_EXPOSURE_VAL:
    qdesc->type = ESP_CAM_SENSOR_PARAM_TYPE_NUMBER;
    qdesc->number.minimum = 2;
    qdesc->number.maximum = 235;  // ❌ Maximum limité à 235
    qdesc->default_value = OV5647_AE_TARGET_DEFAULT;  // = 0x50 = 80
```

#### B. AE Target par défaut = 0x50 (80) - trop élevé

```c
// ov5647.c ligne 26
#define OV5647_AE_TARGET_DEFAULT (0x50)  // = 80 décimal
```

**Problème**: Target AE à 80/255 peut causer **surexposition**, forçant l'ISP à compenser avec un gain élevé → **bruit**.

#### C. Bayer pattern = GBRG (correct pour OV5647)

```c
// ov5647.c ligne 43, 52, 61, 70, 79
.bayer_type = ESP_CAM_SENSOR_BAYER_GBRG,  ✅ Correct pour OV5647
```

**MAIS ATTENTION**: Mon document précédent `OV02C10_CUSTOM_FORMAT_ISSUES.md` dit que OV02C10 utilise GBRG, mais dans ov02c10.c ligne 967, 979, 991, c'est bien **GBRG**!

Donc le Bayer est correct.

#### D. Possible problème de White Balance

OV5647 utilise un système de **banding filter** (anti-flicker 50Hz/60Hz):

```c
// ov5647.c ligne 519-550
static esp_err_t ov5647_set_bandingfilter(esp_cam_sensor_device_t *dev)
{
    // Calcul automatique du filtre anti-scintillement
    // ...
}
```

Si le banding filter est mal calculé, cela peut créer une **teinte de couleur** incorrecte.

### 🎯 SOLUTIONS pour OV5647:

#### Solution 1: Réduire le AE Target (Recommandé)

**Modifier ov5647.c ligne 26**:
```c
#define OV5647_AE_TARGET_DEFAULT (0x30)  // ← Changer de 0x50 à 0x30 (48 au lieu de 80)
```

**Résultat**: Réduction de 37.5% du target AE → moins de surexposition → moins de bruit

#### Solution 2: Ajuster via YAML

```yaml
mipi_dsi_cam:
  id: tab5_cam
  sensor_type: ov5647
  resolution: "1920x1080"
  pixel_format: RGB565
  framerate: 30

  # Réduire l'AE target
  exposure: 48  # ← AE target plus bas (au lieu de 80)
```

#### Solution 3: Vérifier la configuration ISP

Le problème de **couleur rouge** peut venir de l'ISP, pas du sensor!

**Vérifier dans votre configuration**:
```yaml
esp_video:
  id: video
  xclk_pin: GPIO36
  xclk_freq: 24MHz

  # ⚠️ AJOUTER des paramètres ISP
  isp_config:
    awb_mode: auto          # ← White balance automatique
    awb_gain:
      r_gain: 256           # ← Gain rouge (256 = 1.0x, réduire à 200 si trop rouge)
      g_gain: 256           # ← Gain vert
      b_gain: 256           # ← Gain bleu
    denoise: true           # ← Activer le denoising pour réduire le bruit
    sharpen: false          # ← Désactiver sharpening si trop de bruit
```

**Si l'image est trop rouge**, essayez:
```yaml
awb_gain:
  r_gain: 200   # ← Réduire gain rouge de 256 à 200
  g_gain: 256
  b_gain: 280   # ← Augmenter gain bleu pour compenser
```

#### Solution 4: Problème de bruit - Réduire le gain ISP

```yaml
mipi_dsi_cam:
  id: tab5_cam
  sensor_type: ov5647

# lvgl_camera_display ne doit PAS amplifier
lvgl_camera_display:
  id: camera_display
  camera_id: tab5_cam
  canvas_id: camera_canvas
  update_interval: 100ms
  # ⚠️ NE PAS utiliser de gain software supplémentaire
```

---

## 📊 RÉSUMÉ DES PROBLÈMES

| Sensor | Problème | Cause probable | Solution |
|--------|----------|----------------|----------|
| **OV02C10** | Reboot | Formats custom avec erreurs critiques | Utiliser format officiel 1920×1080 1-lane |
| **SC202CS** | Éclairage fort et vert | Exposition max (0x4dc) + gain minimal (0) | Réduire exp_def à 0x300, augmenter gain_def à 32 |
| **OV5647** | Couleur rouge et bruitée | AE target élevé (0x50) + pas de contrôle gain direct | Réduire AE target à 0x30, ajuster AWB ISP |

---

## 🔧 MODIFICATIONS RECOMMANDÉES

### 1. Pour SC202CS (sc202cs.c)

**Ligne 882-884, 893-895, 904-906, 915-917**:
```c
// AVANT:
.gain_def = 0,
.exp_def = 0x4dc,

// APRÈS:
.gain_def = 32,     // ← 2x analog gain
.exp_def = 0x300,   // ← 60% exposition au lieu de 99%
```

### 2. Pour OV5647 (ov5647.c)

**Ligne 26**:
```c
// AVANT:
#define OV5647_AE_TARGET_DEFAULT (0x50)

// APRÈS:
#define OV5647_AE_TARGET_DEFAULT (0x30)  // ← 48 au lieu de 80
```

### 3. Pour OV02C10

**Utiliser le format officiel 1-lane** au lieu des custom formats:

```yaml
mipi_dsi_cam:
  id: tab5_cam
  sensor_type: ov02c10
  sensor_addr: 0x36
  resolution: "1920x1080"  # ← Format officiel 1-lane index 1
  pixel_format: RGB565
  framerate: 30
```

**Commenter les custom formats dans `ov02c10_custom_formats.h`** jusqu'à ce qu'ils soient corrigés avec la vraie datasheet.

---

## ✅ ACTION IMMÉDIATE

1. **Committez les changements actuels** (OV02C10_CUSTOM_FORMAT_ISSUES.md)
2. **Modifiez sc202cs.c** pour corriger gain_def et exp_def
3. **Modifiez ov5647.c** pour corriger AE_TARGET_DEFAULT
4. **Testez chaque sensor** individuellement
5. **Ajustez l'ISP AWB** si OV5647 reste rouge

---

## 📝 NOTES TECHNIQUES

### Bayer Patterns confirmés:
- **OV02C10**: GBRG ✅
- **OV5647**: GBRG ✅
- **SC202CS**: BGGR ✅

### MIPI Lanes confirmées:
- **OV02C10**: 1-lane (format 0,1) ou 2-lane (format 2)
- **OV5647**: 2-lane (tous formats)
- **SC202CS**: 1-lane (tous formats)

### I2C Address (tous):
- **0x36** pour les 3 sensors ✅

### Interface SCCB:
- **Tous utilisent esp_sccb_intf** ✅
  - `esp_sccb_transmit_receive_reg_a16v8()` pour read
  - `esp_sccb_transmit_reg_a16v8()` pour write
