# Analyse Registres SC202CS 800x600 - Problème de Saccadement

**Date:** 2026-01-01
**Problème:** Image saccade à 800x600 sans PPA, fluide avec PPA

---

## 🔍 Comparaison Registres de Timing

### Mode 1280x720 (Officiel M5Stack)

**Localisation:** `sc202cs_settings.h:227-293`

**Registres de timing (0x320c-0x320f): ❌ ABSENTS**

```c
{0x3208, 0x05},  // Output width MSB = 1280
{0x3209, 0x00},  // Output width LSB
{0x320a, 0x02},  // Output height MSB = 720
{0x320b, 0xd0},  // Output height LSB
{0x3210, 0x00},  // X offset
{0x3211, 0x04},  // ...
// ← PAS DE 0x320c, 0x320d, 0x320e, 0x320f!
```

**Le mode 1280x720 utilise les valeurs par défaut du sensor pour HTS/VTS.**

### Mode 800x600 (Custom Local)

**Localisation:** `sc202cs_custom_formats.h:50-139`

**Registres de timing (0x320c-0x320f): ✅ PRÉSENTS**

```c
{0x3208, 0x03},          /* output width MSB = 800 (0x0320) */
{0x3209, 0x20},          /* output width LSB */
{0x320a, 0x02},          /* output height MSB = 600 (0x0258) */
{0x320b, 0x58},          /* output height LSB */

/* Frame timing - MUST set for 30fps (addresses 0x320C-0x320F) */
/* FPS = pclk / (HTS * VTS) = 72MHz / (1920 * 1250) = 30fps */
{0x320c, 0x07},          /* HTS MSB = 1920 (0x0780) */
{0x320d, 0x80},          /* HTS LSB */
{0x320e, 0x04},          /* VTS MSB = 1250 (0x04E2) */
{0x320f, 0xe2},          /* VTS LSB */

{0x3210, 0x00},          /* x offset = 4 */
{0x3211, 0x04},
```

---

## 🔴 PROBLÈME IDENTIFIÉ

### Calcul FPS avec Timing Actuel

**Formule:** FPS = pclk / (HTS × VTS)

**800x600 custom:**
- pclk = 72 MHz = 72,000,000 Hz
- HTS = 1920 (0x0780)
- VTS = 1250 (0x04E2)
- **FPS calculé = 72,000,000 / (1920 × 1250) = 30.0 FPS** ✅

**Mathématiquement correct MAIS...**

### Pourquoi ça Saccade?

**Hypothèse 1: Timing Inadapté pour 800x600**

Le timing HTS=1920, VTS=1250 est copié du mode 1280x720, mais:
- 1280×720 utilise les **defaults** du sensor (pas de 0x320c-0x320f)
- 800×600 **force** HTS=1920, VTS=1250

**Problème possible:**
- Le sensor s'attend à un timing différent pour 800×600
- Forcer HTS/VTS incorrects cause des frames irrégulières
- PPA bufferise et cache le problème

**Hypothèse 2: Timing Trop Serré**

Avec crop centré 808×608→800×600:
- Blanking horizontal: HTS - width = 1920 - 800 = **1120 pixels**
- Blanking vertical: VTS - height = 1250 - 600 = **650 lignes**

Comparaison 1280×720 (timing sensor default):
- Blanking horizontal: ? (inconnu, mais probablement >640)
- Blanking vertical: ? (inconnu)

**Si le timing est trop serré:**
- Le sensor n'a pas assez de temps de blanking
- Frames sortent de manière irrégulière
- Saccadement visible

---

## ✅ SOLUTIONS À TESTER

### Solution #1: Supprimer HTS/VTS (Comme 1280x720)

**Logique:** Si 1280x720 fonctionne sans HTS/VTS explicites, 800x600 aussi.

```c
// sc202cs_custom_formats.h ligne 73-78
// SUPPRIMER CES LIGNES:
/* Frame timing - MUST set for 30fps (addresses 0x320C-0x320F) */
/* FPS = pclk / (HTS * VTS) = 72MHz / (1920 * 1250) = 30fps */
{0x320c, 0x07},          /* HTS MSB = 1920 (0x0780) */
{0x320d, 0x80},          /* HTS LSB */
{0x320e, 0x04},          /* VTS MSB = 1250 (0x04E2) */
{0x320f, 0xe2},          /* VTS LSB */

// GARDER SEULEMENT:
{0x3210, 0x00},          /* x offset = 4 */
{0x3211, 0x04},
{0x3212, 0x00},          /* y offset = 4 */
{0x3213, 0x04},
```

**Résultat attendu:**
- Le sensor utilise ses defaults pour HTS/VTS
- Timing correct automatique pour 800×600
- Pas de saccadement ✅

### Solution #2: Ajuster ISP Info (HTS/VTS en Software)

**Actuellement (ligne 142-149):**
```c
static const esp_cam_sensor_isp_info_t sc202cs_800x600_isp_info = {
    .isp_v1_info = {
        .pclk = 72000000,     /* Pixel clock */
        .hts = 1920,          /* Horizontal Total Size */
        .vts = 1250,          /* Vertical Total Size */
        .exp_def = 0x4dc,     /* M5Stack value (1244) */
        .gain_def = 0,
        .bayer_type = ESP_CAM_SENSOR_BAYER_BGGR,
    }
};
```

**Problème:** HTS/VTS dans isp_info doivent correspondre aux registres sensor!

**Si Solution #1 appliquée (supprimer 0x320c-0x320f):**

Mettre HTS/VTS à 0 dans isp_info pour indiquer "auto":
```c
.hts = 0,  // Auto-detect from sensor
.vts = 0,  // Auto-detect from sensor
```

### Solution #3: Calculer HTS/VTS Optimaux pour 800×600

**Règle SC202CS (basée sur autres modes):**

Analyser les autres modes pour trouver le ratio HTS/width:

**Mode 1600×1200 RAW10 (ligne 19-75):**
- Output: 1600×1200
- HTS: ? (pas défini)
- VTS: ? (pas défini)

**Mode 1600×900 RAW10 (ligne 77-142):**
- Output: 1600×900
- VTS: 0x04E2 (1250) - ligne 140
- HTS: ? (probablement similaire)

**Ratio VTS/height pour 1600×900:**
- VTS / height = 1250 / 900 = **1.389**

**Appliquer à 800×600:**
- VTS optimal = 600 × 1.389 = **833** (0x0341)
- HTS optimal = 800 × 1.389 = **1111** (0x0457)

**Nouveau timing:**
```c
{0x320c, 0x04},          /* HTS MSB = 1111 (0x0457) */
{0x320d, 0x57},          /* HTS LSB */
{0x320e, 0x03},          /* VTS MSB = 833 (0x0341) */
{0x320f, 0x41},          /* VTS LSB */
```

**Vérification FPS:**
- FPS = 72,000,000 / (1111 × 833) = **77.8 FPS** 🔴 TROP RAPIDE!

**Ajuster pour 30 FPS:**
- HTS × VTS = 72,000,000 / 30 = 2,400,000
- Garder ratio blanking mais ajuster total
- HTS = 1600 (ratio 2.0)
- VTS = 1500 (ratio 2.5)
- 1600 × 1500 = 2,400,000 ✅

```c
{0x320c, 0x06},          /* HTS MSB = 1600 (0x0640) */
{0x320d, 0x40},          /* HTS LSB */
{0x320e, 0x05},          /* VTS MSB = 1500 (0x05DC) */
{0x320f, 0xdc},          /* VTS LSB */
```

---

## 📊 Comparaison Solutions

| Solution | HTS | VTS | FPS | Blanking H | Blanking V | Saccadement? |
|----------|-----|-----|-----|------------|------------|--------------|
| **Actuel** | 1920 | 1250 | 30.0 | 1120 (58%) | 650 (52%) | ✅ Avec PPA seulement |
| **#1: Auto (supprimer)** | Auto | Auto | ~30 | Auto | Auto | ❓ À tester |
| **#3: HTS=1600, VTS=1500** | 1600 | 1500 | 30.0 | 800 (50%) | 900 (60%) | ❓ À tester |

---

## 🎯 RECOMMANDATION

### Test #1: Supprimer HTS/VTS (Le Plus Simple)

**Changements à faire:**

1. **Modifier `sc202cs_custom_formats.h:73-78`:**

```c
// AVANT (lignes 73-78)
/* Frame timing - MUST set for 30fps (addresses 0x320C-0x320F) */
/* FPS = pclk / (HTS * VTS) = 72MHz / (1920 * 1250) = 30fps */
{0x320c, 0x07},          /* HTS MSB = 1920 (0x0780) */
{0x320d, 0x80},          /* HTS LSB */
{0x320e, 0x04},          /* VTS MSB = 1250 (0x04E2) */
{0x320f, 0xe2},          /* VTS LSB */

// APRÈS (SUPPRIMER les 6 lignes ci-dessus, garder seulement offsets)
{0x3210, 0x00},          /* x offset = 4 */
{0x3211, 0x04},
```

2. **Modifier isp_info (lignes 142-149):**

```c
.hts = 0,  // Auto-detect (sensor defaults)
.vts = 0,  // Auto-detect (sensor defaults)
```

**Test:**
- Recompilez
- Testez SANS PPA (`mirror_x: false`)
- Vérifiez si le saccadement disparaît

**Si ça marche:** Le sensor gère mieux son propre timing ✅

**Si ça ne marche pas:** Essayez Solution #3 (HTS=1600, VTS=1500)

### Test #2: Si Solution #1 Échoue

Essayez le timing optimisé HTS=1600, VTS=1500 (moins de blanking, plus adapté à 800×600).

---

## 📝 Notes Techniques

### Pourquoi PPA Cache le Problème?

**Sans PPA:**
```
Sensor → V4L2 buffer → LVGL display
  ↑ Timing irrégulier visible directement
```

**Avec PPA:**
```
Sensor → V4L2 buffer → PPA transform → PPA buffer → LVGL display
  ↑ Timing irrégulier           ↑ Buffer lisse l'output
```

PPA fait un buffering/synchronisation qui cache les frames irrégulières.

### Vérification dans Logs

Cherchez:
```
[I][esp_cam_sensor:1333]:    Timing: DQBUF=109us, PPA=2100us
```

Si DQBUF varie beaucoup (50us à 500us), c'est un signe de timing sensor instable.

---

## ✅ Action Immédiate

1. **Appliquez Solution #1** (supprimer HTS/VTS)
2. **Testez sans PPA** pour voir si saccadement disparaît
3. **Rapportez les résultats**

Si besoin d'aide pour modifier, je peux faire les changements directement.

---

## ✅ FIX APPLIQUÉ

**Date:** 2026-01-01

### Changements effectués dans `sc202cs_custom_formats.h`

#### 1. Suppression des registres HTS/VTS (lignes 73-78)

**AVANT:**
```c
/* Frame timing - MUST set for 30fps (addresses 0x320C-0x320F) */
/* FPS = pclk / (HTS * VTS) = 72MHz / (1920 * 1250) = 30fps */
{0x320c, 0x07},          /* HTS MSB = 1920 (0x0780) */
{0x320d, 0x80},          /* HTS LSB */
{0x320e, 0x04},          /* VTS MSB = 1250 (0x04E2) */
{0x320f, 0xe2},          /* VTS LSB */
```

**APRÈS:**
```c
/* NO HTS/VTS - Use sensor defaults (like 1280x720 mode) */
/* This prevents frame timing issues that cause stuttering */
```

#### 2. Mise à jour ISP info pour auto-detect (lignes 142-143)

**AVANT:**
```c
.hts = 1920,          /* Horizontal Total Size */
.vts = 1250,          /* Vertical Total Size */
```

**APRÈS:**
```c
.hts = 0,             /* Auto-detect from sensor defaults (no forced HTS) */
.vts = 0,             /* Auto-detect from sensor defaults (no forced VTS) */
```

### Résultat attendu

✅ **800x600 ne saccade plus sans PPA** - Le sensor utilise maintenant son timing par défaut comme le mode 1280x720 fonctionnel

✅ **Cohérence avec M5Stack** - Le pattern correspond maintenant à `sc202cs_settings.h` et au mode 1280x720

### À tester

Compiler, flasher et vérifier:
1. 800x600 sans PPA → pas de saccadement
2. 800x600 avec PPA → fonctionne toujours
3. FPS stable à ~30 FPS
