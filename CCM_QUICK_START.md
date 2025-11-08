# Guide Rapide: Contrôles CCM pour Correction Couleur

## Nouveau! Contrôle Direct de la Color Correction Matrix (CCM)

Inspiré de ESPHome PR#7639, nous avons ajouté l'accès direct à la CCM de l'ISP via V4L2 pour corriger le problème **blanc→vert** du SC202CS.

---

## ⭐ Méthode la Plus Simple: Configuration YAML

**NOUVEAU!** Vous pouvez maintenant configurer les gains RGB directement dans votre YAML, sans lambda!

### Configuration Recommandée (Auto-apply)

```yaml
mipi_dsi_cam:
  id: tab5_cam
  i2c_id: bsp_bus
  sensor: sc202cs
  resolution: "720P"
  pixel_format: "RGB565"
  framerate: 30

  # Correction couleur automatique (appliquée au démarrage du streaming)
  rgb_gains:
    red: 1.30      # Augmenter rouge (compense dominante verte/cyan)
    green: 0.85    # Réduire vert (corrige blanc→vert) ← CLÉ!
    blue: 1.25     # Augmenter bleu (compense dominante jaune)
```

**Avantages:**
- ✅ Appliqué automatiquement quand vous appelez `start_streaming()`
- ✅ Pas besoin de lambda ou delay manuel
- ✅ Configuration centralisée et propre
- ✅ Valeurs sauvegardées avec votre configuration

**Valeurs disponibles:** 0.1 à 4.0 (défaut: 1.0 = neutre)

---

## 🎯 Méthode Alternative: `set_rgb_gains()` en Lambda

Si vous préférez contrôler manuellement ou changer dynamiquement les gains.

### Usage de Base

```yaml
# Dans votre automation ou on_...:
on_...:
  then:
    # Démarrer streaming
    - lambda: 'id(tab5_cam).start_streaming();'

    # Attendre que streaming soit actif
    - delay: 100ms

    # Corriger blanc→vert avec gains RGB
    - lambda: 'id(tab5_cam).set_rgb_gains(1.3, 0.85, 1.25);'
```

### Valeurs Recommandées SC202CS

**Progression de correction:**

```cpp
// Baseline (problème présent)
id(tab5_cam).set_rgb_gains(1.0, 1.0, 1.0);

// Correction LÉGÈRE (si problème mineur)
id(tab5_cam).set_rgb_gains(1.2, 0.9, 1.15);

// Correction MOYENNE (recommandé pour SC202CS)
id(tab5_cam).set_rgb_gains(1.3, 0.85, 1.25);

// Correction FORTE (configuration M5Stack)
id(tab5_cam).set_rgb_gains(1.5, 1.0, 1.6);
```

### Comprendre les Paramètres

```cpp
set_rgb_gains(red, green, blue)
```

- **`red`**: Gain canal rouge
  - `> 1.0` = Plus de rouge (compense dominante cyan/verte)
  - `< 1.0` = Moins de rouge

- **`green`**: Gain canal vert ← **CLÉ pour blanc→vert!**
  - `< 1.0` = Réduire vert (corrige blanc→vert)
  - `> 1.0` = Plus de vert

- **`blue`**: Gain canal bleu
  - `> 1.0` = Plus de bleu (compense dominante jaune)
  - `< 1.0` = Moins de bleu

---

## 🔧 Méthode Avancée: `set_ccm_matrix()`

Pour correction couleur complexe (matrice 3x3 complète).

### Usage

```cpp
// Matrice 3x3 personnalisée
float matrix[3][3] = {
  {1.3,  0.0,  -0.1},  // R_out = 1.3*R_in + 0.0*G_in + -0.1*B_in
  {-0.1, 0.85,  0.0},  // G_out = -0.1*R_in + 0.85*G_in + 0.0*B_in
  {0.0,  -0.1,  1.25}  // B_out = 0.0*R_in + -0.1*G_in + 1.25*B_in
};

id(tab5_cam).set_ccm_matrix(matrix);
```

**Note:** `set_rgb_gains()` est un wrapper qui crée une matrice diagonale. Utilisez `set_ccm_matrix()` seulement si vous avez besoin de cross-channel correction.

---

## 🌡️ Méthode Complémentaire: `set_wb_gains()`

Contrôle les gains White Balance hardware de l'ISP (avant CCM).

### Usage

```cpp
// Gains WB (rouge et bleu, vert fixe à 1.0)
id(tab5_cam).set_wb_gains(1.0, 1.0);  // Neutre

// Compenser lumière incandescente (jaune)
id(tab5_cam).set_wb_gains(0.7, 1.8);

// Compenser fluorescent (dominante verte)
id(tab5_cam).set_wb_gains(1.3, 0.9);
```

**Différence avec `set_rgb_gains()`:**
- `set_wb_gains()`: Gains hardware **avant** demosaic (pipeline précoce)
- `set_rgb_gains()`: Gains CCM **après** demosaic (pipeline tardif)

**Recommandation:** Utilisez `set_rgb_gains()` pour la correction couleur principale.

---

## 📋 Exemple Complet: Correction Optimale SC202CS

```yaml
sensor:
  - platform: mipi_dsi_cam
    id: tab5_cam
    sensor_type: "sc202cs"
    # ... autres configs ...

# Bouton pour corriger l'image
button:
  - platform: template
    name: "Corriger Couleurs"
    on_press:
      - lambda: |-
          // 1. Corriger exposition (réduire si trop clair)
          id(tab5_cam).set_exposure(10000);
          id(tab5_cam).set_gain(16000);  // SC202CS: 16x recommandé

          // 2. Corriger blanc→vert avec CCM
          id(tab5_cam).set_rgb_gains(1.3, 0.85, 1.25);

          ESP_LOGI("camera", "✅ Correction couleur appliquée");

# Automation au démarrage streaming
on_...:
  then:
    - lambda: 'id(tab5_cam).start_streaming();'
    - delay: 100ms

    # Appliquer corrections automatiquement
    - lambda: |-
        id(tab5_cam).set_exposure(10000);
        id(tab5_cam).set_gain(16000);
        id(tab5_cam).set_rgb_gains(1.3, 0.85, 1.25);
```

---

## 🧪 Guide de Calibration Personnalisée

### Étape 1: Baseline

```cpp
// Commencer avec identité
id(tab5_cam).set_rgb_gains(1.0, 1.0, 1.0);
```

### Étape 2: Identifier le Problème

**Symptôme: Blanc apparaît VERT**
- ✅ Trop de vert dans l'image
- 🔧 Solution: Réduire gain vert, augmenter rouge/bleu

**Symptôme: Blanc apparaît JAUNE**
- ✅ Pas assez de bleu
- 🔧 Solution: Augmenter gain bleu

**Symptôme: Blanc apparaît BLEUTÉ/CYAN**
- ✅ Trop de bleu, pas assez de rouge
- 🔧 Solution: Réduire bleu, augmenter rouge

### Étape 3: Ajustement Progressif

```cpp
// Si blanc→vert, commencer par:
id(tab5_cam).set_rgb_gains(1.0, 0.9, 1.0);  // Réduire vert 10%

// Toujours vert? Réduire plus:
id(tab5_cam).set_rgb_gains(1.1, 0.85, 1.1); // Vert -15%, R/B +10%

// Continuer jusqu'à blanc neutre:
id(tab5_cam).set_rgb_gains(1.3, 0.85, 1.25); // Configuration finale
```

### Étape 4: Validation

**Test avec cible blanche:**
- Placer feuille blanche devant caméra
- Blanc doit apparaître blanc (pas de teinte verte/jaune/bleue)
- Ajuster jusqu'à satisfaction

---

## 🔍 Diagnostic

### Vérifier que CCM est Appliquée

```cpp
// Après set_rgb_gains(), chercher dans les logs:
[mipi_dsi_cam] ✓ CCM matrix configured:
[mipi_dsi_cam]   [1.30, 0.00, 0.00]
[mipi_dsi_cam]   [0.00, 0.85, 0.00]
[mipi_dsi_cam]   [0.00, 0.00, 1.25]
[mipi_dsi_cam] ✓ RGB gains: R=1.30, G=0.85, B=1.25
```

### CCM Ne S'applique Pas?

**Vérifier:**
1. Streaming actif? (`start_streaming()` appelé?)
2. Délai suffisant avant CCM? (attendre 100ms après `start_streaming()`)
3. Erreur dans logs? Vérifier `errno`

### Erreur Commune

```
Cannot set CCM matrix: streaming not active
```

**Solution:**
```cpp
// ❌ INCORRECT
id(tab5_cam).set_rgb_gains(1.3, 0.85, 1.25);  // Streaming pas actif!
id(tab5_cam).start_streaming();

// ✅ CORRECT
id(tab5_cam).start_streaming();
delay(100);  // Attendre que streaming soit actif
id(tab5_cam).set_rgb_gains(1.3, 0.85, 1.25);
```

---

## 🔄 YAML vs Lambda: Quelle Méthode Choisir?

### Comparaison Rapide

| Aspect | Configuration YAML | Lambda `set_rgb_gains()` |
|--------|-------------------|--------------------------|
| **Simplicité** | ⭐⭐⭐⭐⭐ Très simple | ⭐⭐⭐ Moyen |
| **Auto-apply** | ✅ Automatique au streaming | ❌ Manuel avec delay |
| **Modification runtime** | ❌ Nécessite recompile | ✅ Changement dynamique |
| **Debugging** | ⚠️ Valeurs fixes | ✅ Test rapide de valeurs |
| **Production** | ✅ Recommandé | ⚠️ OK si besoin dynamique |

### Quand Utiliser YAML (Recommandé pour la plupart des cas)

✅ **Utilisez la configuration YAML si:**
- Vous connaissez les bonnes valeurs pour votre capteur/éclairage
- Vous voulez une solution "set and forget" (configurer et oublier)
- Vous déployez en production avec des valeurs stables
- Vous préférez une configuration propre et centralisée

**Exemple production:**
```yaml
mipi_dsi_cam:
  sensor: sc202cs
  rgb_gains:
    red: 1.30
    green: 0.85
    blue: 1.25
  # Plus besoin de lambda!
```

### Quand Utiliser Lambda

✅ **Utilisez lambda `set_rgb_gains()` si:**
- Vous testez différentes valeurs pour calibration
- Vous changez les gains selon l'heure du jour (éclairage variable)
- Vous voulez des contrôles runtime via boutons/sliders
- Vous faites du debugging ou des tests A/B

**Exemple calibration/test:**
```yaml
button:
  - name: "Test Gains 1"
    on_press:
      - lambda: 'id(cam).set_rgb_gains(1.2, 0.9, 1.15);'
  - name: "Test Gains 2"
    on_press:
      - lambda: 'id(cam).set_rgb_gains(1.3, 0.85, 1.25);'
  - name: "Test Gains 3"
    on_press:
      - lambda: 'id(cam).set_rgb_gains(1.5, 1.0, 1.6);'
```

### Combiner les Deux (Avancé)

Vous pouvez avoir une configuration YAML par défaut ET la surcharger avec lambda:

```yaml
mipi_dsi_cam:
  id: cam
  rgb_gains:
    red: 1.30     # Valeur par défaut (jour)
    green: 0.85
    blue: 1.25

# Surcharger selon l'heure
time:
  - platform: homeassistant
    id: ha_time
    on_time:
      # Matin (6h): Lumière froide → plus de rouge
      - hours: 6
        then:
          - lambda: 'id(cam).set_rgb_gains(1.5, 0.85, 1.1);'

      # Soir (18h): Lumière chaude → moins de rouge
      - hours: 18
        then:
          - lambda: 'id(cam).set_rgb_gains(1.2, 0.85, 1.4);'
```

**Note:** Les appels lambda surchargent la config YAML jusqu'au prochain redémarrage.

---

## 📊 Comparaison Approches

| Méthode | Complexité | Puissance | Usage Recommandé |
|---------|------------|-----------|------------------|
| `set_exposure()` + `set_gain()` | Faible | Faible | Corriger surexposition (TOUJOURS faire en premier) |
| `set_white_balance_temp()` | Faible | Faible | Ajustement température couleur global |
| **`set_rgb_gains()`** | Moyenne | **Élevée** | **Corriger blanc→vert (RECOMMANDÉ)** |
| `set_wb_gains()` | Moyenne | Moyenne | Compenser type d'éclairage spécifique |
| `set_ccm_matrix()` | Élevée | Très élevée | Correction couleur complexe avec crosstalk |

---

## ⚡ Pipeline de Correction Optimal

**Ordre recommandé d'application:**

```cpp
// 1. TOUJOURS commencer par exposition/gain
id(tab5_cam).set_exposure(10000);
id(tab5_cam).set_gain(16000);

// 2. Optionnel: WB hardware si éclairage spécifique connu
// id(tab5_cam).set_wb_gains(1.0, 1.0);  // Généralement pas nécessaire

// 3. Correction couleur finale avec CCM
id(tab5_cam).set_rgb_gains(1.3, 0.85, 1.25);
```

**Pourquoi cet ordre?**

```
Capteur → [Gain] → Demosaic → [WB gains] → [CCM RGB] → Output
           ↑                     ↑            ↑
         set_gain()         set_wb_gains() set_rgb_gains()
```

- **Gain** affect la luminosité brute (faire en premier)
- **WB gains** compensent l'éclairage (optionnel)
- **CCM** corrige les couleurs finales (dernier, plus précis)

---

## 🚀 Migration depuis Ancienne API

**Avant (méthodes limitées):**
```cpp
id(tab5_cam).set_exposure(10000);
id(tab5_cam).set_white_balance_temp(5500);  // Correction limitée
```

**Après (avec CCM):**
```cpp
id(tab5_cam).set_exposure(10000);
id(tab5_cam).set_rgb_gains(1.3, 0.85, 1.25);  // Correction précise!
```

**Résultat:** Blanc apparaît blanc, pas vert! ✅

---

## 📚 Références

- **ESPHome PR#7639:** Architecture originale avec contrôles CCM
- **`ESPHOME_PR7639_ISP_ANALYSIS.md`:** Analyse détaillée de la PR
- **`FIX_EXPOSURE_AND_FPS.md`:** Documentation complète des contrôles
- **ESP-IDF ISP API:** `esp_video_isp_ioctl.h` - Structures CCM/WB

---

## ✅ Checklist Correction Couleur

- [ ] Streaming démarré (`start_streaming()`)
- [ ] Exposition corrigée (`set_exposure()`)
- [ ] Gain ajusté (`set_gain()`)
- [ ] CCM appliquée (`set_rgb_gains()`)
- [ ] Blanc apparaît blanc (validation visuelle)
- [ ] Logs confirment application (`✓ RGB gains`)

**Si blanc toujours vert après ces étapes:**
- Essayer correction plus forte: `set_rgb_gains(1.5, 1.0, 1.6)`
- Vérifier éclairage ambiant (fluorescent peut causer dominante verte)
- Envisager capteur OV5647/OV02C10 avec calibration JSON complète
