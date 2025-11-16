# SC202CS - Fix pour Image Trop Claire / Surexposée

## 🎯 Problème Identifié

Le sensor **SC202CS** en mode **1280x720 RAW8 @ 30fps** produit des images **trop claires/surexposées**.

### Pourquoi?

Contrairement à l'**OV5647** qui fonctionne bien, le SC202CS a plusieurs limitations :

| Caractéristique | OV5647 | SC202CS |
|-----------------|--------|---------|
| **Auto-Exposition ISP** | ✅ Supportée via V4L2 | ❌ NON supportée |
| **V4L2_CID_EXPOSURE** | ✅ Fonctionne | ❌ Ignoré (sensor gère en interne) |
| **V4L2_CID_GAIN** | ✅ Fonctionne | ❌ Ignoré |
| **V4L2_CID_BRIGHTNESS** | ✅ Fonctionne | ✅ Fonctionne (ISP) |
| **AWB (White Balance)** | ✅ V4L2 supporté | ❌ Géré par registres sensor |

### Registres d'Exposition Hardcodés

Le problème est dans les **registres d'initialisation du sensor** :

```c
// Fichier: sc202cs_settings.h ligne 208-209
{0x3e00, 0x00},  // SHUTTER_TIME_H (exposition HIGH byte)
{0x3e01, 0x4d},  // SHUTTER_TIME_M (exposition MID byte)  ← TROP ÉLEVÉ!
{0x3e02, 0xc0},  // SHUTTER_TIME_L (exposition LOW byte)
{0x3e09, 0x00},  // ANG_GAIN (analog gain = 1x)
```

**Valeur totale** : `0x004dc0` = **19904** (décimal) ➡️ **TROP ÉLEVÉ** pour la plupart des environnements!

Les valeurs `gain_def` et `exp_def` dans `sc202cs_isp_info[]` sont **IGNORÉES** car le sensor ne supporte pas l'auto-exposition de l'ISP.

---

## ✅ Solution Implémentée

### Format Custom avec Exposition Réduite

Un nouveau format custom a été créé qui **modifie directement les registres du sensor** au démarrage.

**Fichier** : `components/mipi_dsi_cam/sc202cs_custom_formats.h`

```c
// Exposition RÉDUITE à 25% de la valeur par défaut
{0x3e00, 0x00},  // SHUTTER_TIME_H = 0x00
{0x3e01, 0x13},  // SHUTTER_TIME_M = 0x13 (au lieu de 0x4d)
{0x3e02, 0x70},  // SHUTTER_TIME_L = 0x70 (au lieu de 0xc0)
{0x3e09, 0x00},  // Analog gain = 1x (inchangé)
```

**Nouvelle valeur** : `0x001370` = **4976** (25% de l'originale)

---

## 📝 Configuration YAML

### Ancienne Configuration (image trop claire):

```yaml
mipi_dsi_cam:
  id: tab5_cam
  sensor_type: sc202cs
  resolution: "720P"  # ← Utilise exposition par défaut (trop élevée)
  pixel_format: RGB565
  framerate: 30
```

### Nouvelle Configuration (exposition correcte):

```yaml
mipi_dsi_cam:
  id: tab5_cam
  sensor_type: sc202cs
  resolution: "720P_LOW_EXPOSURE"  # ← Utilise format custom avec exposition réduite
  pixel_format: RGB565
  framerate: 30
```

### Configuration Complète avec Autres Ajustements:

```yaml
mipi_dsi_cam:
  id: tab5_cam
  i2c_id: bsp_bus
  sensor_type: sc202cs
  resolution: "720P_LOW_EXPOSURE"  # Format custom avec exposition réduite
  pixel_format: RGB565
  framerate: 30

  # Ajustements supplémentaires (optionnels)
  camera_controls:
    # Réduire encore la luminosité via ISP si nécessaire
    - id: V4L2_CID_BRIGHTNESS
      initial_value: -20  # -50 à +50 (négatif = plus sombre)

    # Ajuster contraste si nécessaire
    - id: V4L2_CID_CONTRAST
      initial_value: 110  # 0-255 (128 = défaut)
```

---

## 📊 Résultats Attendus

### Avant (Default "720P"):
- Exposition : 19904 (0x4dc0)
- Résultat : Image **trop claire**, détails perdus dans les zones lumineuses
- Logs : `Using standard format: 720P 1280x720`

### Après ("720P_LOW_EXPOSURE"):
- Exposition : 4976 (0x1370) = **25% du défaut**
- Résultat : Image **correctement exposée**, meilleur équilibre lumière/ombre
- Logs :
  ```
  ✅ Using CUSTOM format: 720P 1280x720 RAW8 @ 30fps LOW EXPOSURE (SC202CS)
     Exposition réduite à 25% (0x1370 au lieu de 0x4dc0)
  ✅ Custom format applied successfully!
  ```

---

## 🔧 Ajustements Personnalisés

Si l'exposition à 25% est encore trop claire ou trop sombre, vous pouvez :

### Option 1: Créer Votre Propre Variant

Modifiez `components/mipi_dsi_cam/sc202cs_custom_formats.h` :

```c
// Pour exposition à 50% (9952 = 0x26e0):
{0x3e01, 0x26},  // SHUTTER_TIME_M
{0x3e02, 0xe0},  // SHUTTER_TIME_L

// Pour exposition à 12% (2560 = 0x0a00):
{0x3e01, 0x0a},  // SHUTTER_TIME_M
{0x3e02, 0x00},  // SHUTTER_TIME_L

// Pour exposition à 37% (7400 = 0x1ce8):
{0x3e01, 0x1c},  // SHUTTER_TIME_M
{0x3e02, 0xe8},  // SHUTTER_TIME_L
```

### Option 2: Combiner avec Brightness ISP

```yaml
camera_controls:
  # Si l'image custom est encore un peu claire
  - id: V4L2_CID_BRIGHTNESS
    initial_value: -30  # Assombrir via ISP
```

---

## 🧮 Calcul de l'Exposition

**Formule** : Exposition (hex) = Exposition (decimal) converti en hex 24-bit

Exemples:
- 10% de 19904 = **1990** = `0x0007c6`
- 25% de 19904 = **4976** = `0x001370` ✅ (implémenté)
- 50% de 19904 = **9952** = `0x0026e0`
- 75% de 19904 = **14928** = `0x003a50`

**Pour modifier** :
```c
{0x3e00, HIGH_BYTE},
{0x3e01, MID_BYTE},
{0x3e02, LOW_BYTE},
```

---

## 🆚 Comparaison Sensor SC202CS vs OV5647

### Pourquoi OV5647 fonctionne "out of the box"?

L'OV5647 **délègue** la gestion de l'exposition à l'**ISP** via V4L2 :
- ✅ Auto-Exposition via `V4L2_CID_EXPOSURE_AUTO`
- ✅ Ajustements en temps réel via `V4L2_CID_EXPOSURE`
- ✅ AWB dynamique via `V4L2_CID_AUTO_WHITE_BALANCE`

### Limitations SC202CS

Le SC202CS gère l'exposition **EN INTERNE** via ses propres registres :
- ❌ Pas d'auto-exposition V4L2
- ❌ Registres d'exposition fixés au démarrage
- ❌ AWB géré par registres internes (pas via V4L2)
- ✅ **Solution** : Programmer les registres directement avec format custom

---

## 📚 Fichiers Modifiés

| Fichier | Changement |
|---------|------------|
| `sc202cs_custom_formats.h` | Nouveaux formats avec registres exposition réduite |
| `mipi_dsi_cam.cpp` | Détection `"720P_LOW_EXPOSURE"` + application format custom |
| `mipi_dsi_cam.h` | (aucun changement requis) |
| `__init__.py` | (aucun changement requis) |

---

## 🚀 Test et Validation

### Étape 1: Modifier votre YAML
```yaml
resolution: "720P_LOW_EXPOSURE"
```

### Étape 2: Compiler et flasher
```bash
esphome run tab5.yaml
```

### Étape 3: Vérifier les logs
Cherchez dans les logs :
```
✅ Using CUSTOM format: 720P 1280x720 RAW8 @ 30fps LOW EXPOSURE (SC202CS)
   Exposition réduite à 25% (0x1370 au lieu de 0x4dc0)
✅ Custom format applied successfully!
```

### Étape 4: Comparer l'image

**Avant** (720P) : Trop claire, détails perdus
**Après** (720P_LOW_EXPOSURE) : Exposition équilibrée

### Si encore trop clair/sombre

Ajustez via `camera_controls`:
```yaml
camera_controls:
  - id: V4L2_CID_BRIGHTNESS
    initial_value: -40  # Ajustez entre -50 et +50
```

---

## ⚠️ Notes Importantes

1. **V4L2 Controls ne fonctionnent PAS pour exposition**
   - `V4L2_CID_EXPOSURE` est **ignoré** par SC202CS
   - `V4L2_CID_GAIN` est **ignoré**
   - Seuls les registres sensor comptent

2. **Format Custom = Registres Sensor Modifiés**
   - Les formats custom programment directement le sensor
   - Changement permanent jusqu'au prochain reboot
   - Pas d'ajustement dynamique possible

3. **Brightness ISP Fonctionne Toujours**
   - `V4L2_CID_BRIGHTNESS` agit APRÈS le sensor (sur l'ISP)
   - Peut être utilisé pour ajustement final
   - Plage : -128 à +127 (pratique : -50 à +50)

4. **Compatibilité**
   - Testé sur ESP32-P4 avec ESP-IDF 5.4+
   - SC202CS firmware version standard
   - Fonctionne en RGB565 et autres formats ISP

---

## 🎯 Conclusion

Le SC202CS nécessite une **approche différente** de l'OV5647 car il ne supporte pas l'auto-exposition V4L2. La solution est de **programmer directement les registres du sensor** via un format custom.

**Résolution recommandée** : `"720P_LOW_EXPOSURE"` (exposition à 25% du défaut)

Cette approche contourne la limitation matérielle du sensor et permet d'obtenir une exposition correcte similaire à celle de l'OV5647.
