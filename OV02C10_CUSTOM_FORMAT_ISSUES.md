# PROBLÈMES CRITIQUES - OV02C10 Custom Formats

## ⚠️ CAUSE PROBABLE DU REBOOT

Le reboot de l'OV02C10 avec les formats custom (800×480 et 1280×800) est très probablement causé par **plusieurs erreurs critiques** dans les configurations.

---

## 🔴 ERREUR CRITIQUE #1: MAUVAIS BAYER PATTERN

### Problème
Les formats custom utilisent **BGGR** alors que l'OV02C10 utilise **GBRG**.

### Preuve (ov02c10.c ligne 967, 979, 991):
```c
static const esp_cam_sensor_isp_info_t ov02c10_isp_info[] = {
    {
        .isp_v1_info = {
            .bayer_type = ESP_CAM_SENSOR_BAYER_GBRG,  // ✅ CORRECT
        }
    },
    // ... tous les 3 formats officiels utilisent GBRG
};
```

### Dans ov02c10_custom_formats.h (INCORRECT):
```c
static const esp_cam_sensor_isp_info_t ov02c10_800x480_isp_info = {
    .isp_v1_info = {
        .bayer_type = ESP_CAM_SENSOR_BAYER_BGGR,  // ❌ MAUVAIS !
    }
};

static const esp_cam_sensor_isp_info_t ov02c10_1280x800_isp_info = {
    .isp_v1_info = {
        .bayer_type = ESP_CAM_SENSOR_BAYER_BGGR,  // ❌ MAUVAIS !
    }
};
```

### Impact
- L'ISP (Image Signal Processor) va mal interpréter les données RAW10
- Couleurs complètement fausses ou corruption de mémoire
- **Crash probable de l'ISP → Watchdog timeout → REBOOT**

### Solution
```c
.bayer_type = ESP_CAM_SENSOR_BAYER_GBRG,  // Changer de BGGR à GBRG
```

---

## 🔴 ERREUR CRITIQUE #2: REGISTRES NON TESTÉS (TEMPLATES)

### Commentaire dans le code (ligne 91, 166):
```c
// Note: Ces registres sont des TEMPLATES basés sur les formats standard OV02C10.
//       Ils devront être ajustés selon le datasheet OV02C10 réel
```

**Cela signifie**: Les configurations de registres ne sont PAS vérifiées avec la datasheet OV02C10 réelle!

### Problèmes potentiels:
1. **Registres PLL incorrects** → horloge mal configurée → crash
2. **Timing (HTS/VTS) invalides** → ISP ne peut pas synchroniser → timeout
3. **Séquence d'initialisation manquante** → capteur mal configuré

---

## 🔴 ERREUR CRITIQUE #3: RESET SANS DÉLAI

### Format officiel (ov02c10_settings.h ligne 36-42):
```c
static const ov02c10_reginfo_t ov02c10_mipi_reset_regs[] = {
    {0x0100, 0x00}, // enable sleep
    {0x0103, 0x01}, // software reset
    {OV02C10_REG_DELAY, 0x0a},  // ✅ DÉLAI de 10ms OBLIGATOIRE
    {0x4800, BIT(0)},
    {OV02C10_REG_END, 0x00},
};
```

### Format custom 800×480 (ligne 9-10):
```c
static const ov02c10_reginfo_t ov02c10_800x480_raw10_30fps[] = {
    {0x0103, 0x01},  // Software reset
    {0x0100, 0x00},  // Standby
    // ❌ PAS DE DÉLAI après reset !
    {0x0302, 0x32},  // Configuration PLL immédiate → ERREUR !
```

### Impact
- Le capteur n'a pas le temps de se réinitialiser
- Les registres suivants sont écrits pendant que le capteur est en reset
- **Configuration corrompue → Crash**

---

## 🔴 ERREUR CRITIQUE #4: CONFIGURATION PLL INCORRECTE

### Formats officiels 2-lane (ligne 504-505):
```c
{0X0303, 0X06},  // PLL divider
{0X0305, 0X90},  // PLL multiplier = 144 (0x90)
```

### Format custom 800×480 (ligne 11-12):
```c
{0x0302, 0x32},  // ❌ Registre différent (0x0302 vs 0x0305)
{0x030e, 0x02},  // ❌ Registre non utilisé dans les formats officiels
```

### Calcul PCLK officiel:
```
XCLK = 24 MHz
PLL multiplier = 0x90 = 144
Dividers = 0x06
→ PCLK calculé ≈ 81.6 MHz
```

### Calcul PCLK custom:
```
HTS × VTS × FPS = 1050 × 526 × 30 = 16,569,000 Hz = 16.6 MHz
```

**Problème**: Le PCLK custom (16.6 MHz) est **5× plus lent** que le PCLK officiel (81.6 MHz)!

---

## 🔴 ERREUR #5: HTS/VTS TROP PETITS

### Formats officiels:
| Format      | HTS  | VTS  | Ratio        |
|-------------|------|------|--------------|
| 1288×728    | 2280 | 1164 | HTS/width=1.77 |
| 1920×1080   | 2280 | 1164 | HTS/width=1.19 |

### Formats custom:
| Format      | HTS  | VTS  | Ratio        |
|-------------|------|------|--------------|
| 800×480     | 1050 | 526  | HTS/width=1.31 ❌ |
| 1280×800    | 1500 | 850  | HTS/width=1.17 ❌ |

**Problème**: Les blanking intervals (HTS-width, VTS-height) sont probablement trop courts pour:
- La synchronisation MIPI
- Le processing de l'ISP
- Les registres internes du capteur

---

## 🔴 ERREUR #6: MIPI CLOCK INCORRECT

### Format officiel (ov02c10_settings.h ligne 19-26):
```c
#define OV02C10_IDI_CLOCK_RATE_800x800_50FPS        (100000000ULL)
#define OV02C10_MIPI_CSI_LINE_RATE_800x800_50FPS    (OV02C10_IDI_CLOCK_RATE * 4)
                                                    // = 400 MHz
```

### Format custom 800×480 (ligne 59):
```c
.mipi_clk = 300000000,  // ❌ 300 MHz au lieu de 400 MHz
```

**Problème**: La MIPI clock doit être calculée en fonction du bit rate nécessaire:
```
Bit rate = width × height × bits_per_pixel × fps
         = 800 × 480 × 10 × 30
         = 115,200,000 bits/s

MIPI clock (2-lane) = bit_rate / 2 / 2
                    = 115,200,000 / 4
                    = 28.8 MHz

Mais avec overhead MIPI (start/end packets, blanking):
→ Besoin d'au moins 300-400 MHz
```

Le 300 MHz pourrait être trop juste!

---

## 📊 COMPARAISON DÉTAILLÉE

| Paramètre          | Officiel (1920×1080) | Custom (800×480) | Status |
|--------------------|---------------------|------------------|--------|
| Bayer pattern      | **GBRG**            | BGGR             | ❌     |
| PCLK               | 81.6 MHz            | 16.6 MHz         | ❌     |
| HTS                | 2280                | 1050             | ⚠️     |
| VTS                | 1164                | 526              | ⚠️     |
| MIPI clock         | 400 MHz             | 300 MHz          | ⚠️     |
| PLL config         | 0x0305=0x90         | 0x0302=0x32      | ❌     |
| Reset delay        | ✅ 10ms             | ❌ None          | ❌     |
| Registers tested   | ✅ Verified         | ❌ Template      | ❌     |

---

## ✅ SOLUTION RECOMMANDÉE

### Option 1: UTILISER FORMAT OFFICIEL (Recommandé)

**Utilisez le format officiel 1920×1080 2-lane** qui est testé et fonctionne:

```yaml
mipi_dsi_cam:
  id: tab5_cam
  i2c_id: bsp_bus
  sensor_type: ov02c10
  sensor_addr: 0x36
  resolution: "1920x1080"  # ✅ Format officiel testé
  pixel_format: RGB565
  framerate: 30

lvgl_camera_display:
  id: camera_display
  camera_id: tab5_cam
  canvas_id: camera_canvas
  auto_resize: true        # ✅ L'ISP va redimensionner à 800×480
  update_interval: 100ms
```

**Avantages**:
- ✅ Registres vérifiés et testés
- ✅ Pas de crash
- ✅ L'ISP peut faire le downscaling hardware de 1920×1080 → 800×480
- ✅ Meilleure qualité d'image (plus de détails capturés)

---

### Option 2: CORRIGER LE FORMAT CUSTOM 800×480

Si vous voulez absolument utiliser 800×480 natif, il faut:

1. **Changer le Bayer pattern**:
```c
.bayer_type = ESP_CAM_SENSOR_BAYER_GBRG,  // ← GBRG au lieu de BGGR
```

2. **Ajouter un délai après reset**:
```c
static const ov02c10_reginfo_t ov02c10_800x480_raw10_30fps[] = {
    {0x0100, 0x00},  // Standby first
    {0x0103, 0x01},  // Software reset
    {OV02C10_REG_DELAY, 0x0a},  // ← AJOUTER 10ms delay
    // ... rest of config
```

3. **Utiliser les mêmes registres PLL que le format officiel**:
```c
{0x0301, 0x08},  // Copier du format officiel
{0x0303, 0x06},
{0x0304, 0x01},
{0x0305, 0x90},  // ← Même PLL que format officiel
```

4. **Augmenter HTS/VTS** pour avoir plus de blanking:
```c
.pclk = 81666700,  // ← Même PCLK que format officiel
.hts = 2280,       // ← Augmenter (même que officiel)
.vts = 1000,       // ← Augmenter pour avoir du blanking
```

5. **Mettre MIPI clock à 400 MHz**:
```c
.mipi_clk = 400000000,  // ← 400MHz comme format officiel
```

**⚠️ ATTENTION**: Même avec ces corrections, il faudra **tester et ajuster les registres** selon la vraie datasheet OV02C10!

---

## 🎯 ACTION IMMÉDIATE

**POUR TESTER MAINTENANT**:

1. Commentez les formats custom dans `ov02c10_custom_formats.h`
2. Utilisez le format officiel `1920x1080`:

```yaml
mipi_dsi_cam:
  id: tab5_cam
  sensor_type: ov02c10
  resolution: "1920x1080"  # Format officiel
  pixel_format: RGB565
```

3. Laissez l'ISP faire le downscaling automatique vers votre canvas 800×480

**Si le reboot persiste avec le format officiel**, alors le problème est ailleurs (watchdog timeout LVGL, problème I2C, XCLK, etc.).

**Si le reboot disparaît avec le format officiel**, alors c'est **confirmé** que les formats custom sont la cause.

---

## 📝 RÉSUMÉ

| Issue | Severity | Impact sur reboot |
|-------|----------|-------------------|
| Bayer pattern BGGR au lieu de GBRG | 🔴 Critique | Très élevé - ISP crash |
| Registres template non testés | 🔴 Critique | Élevé - config invalide |
| Pas de délai après reset | 🔴 Critique | Élevé - corruption config |
| PLL mal configuré | 🟠 Majeur | Moyen - timing incorrect |
| HTS/VTS trop petits | 🟡 Mineur | Faible - image glitchy |
| MIPI clock 300MHz | 🟡 Mineur | Faible - bandwidth limit |

**Conclusion**: Les formats custom ont **3 erreurs critiques** qui causent très probablement le reboot.

**Recommandation**: Utiliser le format officiel 1920×1080 et laisser l'ISP faire le scaling.
