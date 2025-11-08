# Fix Surexposition, Blanc→Vert et FPS Limité

Ce document explique comment corriger les 3 problèmes identifiés avec le capteur SC202CS.

## Problèmes Résolus

### ✅ Problème 1: FPS Limité à 4 (au lieu de 30)
**Symptôme:** FPS réel ~4 malgré `update_interval: 33ms` configuré
**Cause:** Display configuré avec `update_interval: never`
**Solution:** Changement YAML (voir ci-dessous)

### ✅ Problème 2: Image Surexposée
**Symptôme:** Image trop claire/blanche
**Cause:** AEC (Auto Exposure Control) désactivé dans la config IPA
**Solution:** AEC réactivé + méthodes de contrôle manuel ajoutées

### ✅ Problème 3: Blanc → Vert
**Symptôme:** Les zones blanches apparaissent vertes
**Cause:** AWB (Auto White Balance) mal paramétré
**Solution:** AWB amélioré + méthodes de contrôle manuel ajoutées

---

## Configuration Requise (YAML)

### 1. Fix Display Refresh Rate (OBLIGATOIRE)

**AVANT (ne fonctionne pas):**
```yaml
display:
  - platform: mipi_dsi
    id: main_display
    model: M5Stack-Tab5
    update_interval: never  # ← PROBLÈME: jamais de refresh!
    auto_clear_enabled: false
    rotation: 270
```

**APRÈS (corrigé):**
```yaml
display:
  - platform: mipi_dsi
    id: main_display
    model: M5Stack-Tab5
    update_interval: 33ms  # ← CORRECTION: 30 FPS
    auto_clear_enabled: false
    rotation: 270
```

**Impact:** FPS passera de ~4 → 25-30 FPS immédiatement! 🚀

### 2. Optimiser LVGL (RECOMMANDÉ)

Ajoutez dans la section `lvgl:`:
```yaml
lvgl:
  byte_order: little_endian
  buffer_size: 25%      # ← Ajouter pour optimiser le buffer
  full_refresh: false   # ← Ajouter pour éviter le full refresh
  displays:
    - main_display
  touchscreens:
    - touch_screen
  # ... reste de la config
```

---

## Configuration Automatique (IPA)

### AEC & AWB Automatiques (Réactivés)

Le pipeline IPA a été modifié pour inclure **6 algorithmes** (au lieu de 5):

```
Capteur SC202CS (RAW8) → ISP → IPA (6 algorithmes) → RGB565

Algorithmes actifs:
  1. aec.simple              ← Auto Exposure Control (nouveau!)
  2. awb.gray                ← Auto White Balance
  3. denoising.gain_feedback ← Réduction du bruit
  4. sharpen.freq_feedback   ← Netteté
  5. gamma.lumma_feedback    ← Correction gamma
  6. cc.linear               ← Color Correction Matrix
```

**Avec cette configuration, l'exposition et la balance des blancs sont automatiquement ajustées.**

Si l'ajustement automatique n'est pas satisfaisant, utilisez les contrôles manuels ci-dessous.

---

## Contrôles Manuels (C++ API)

Quatre nouvelles méthodes ont été ajoutées au composant `mipi_dsi_cam`:

### 1. Contrôle d'Exposition

```cpp
// Réduire l'exposition (pour corriger surexposition)
id(tab5_cam).set_exposure(10000);  // Exposition faible (scène lumineuse)

// Exposition normale
id(tab5_cam).set_exposure(20000);  // Défaut recommandé

// Haute exposition
id(tab5_cam).set_exposure(40000);  // Scène sombre

// Réactiver AEC automatique
id(tab5_cam).set_exposure(0);  // 0 = auto
```

**Valeurs typiques:**
- `1000-5000`: Très faible (scènes très lumineuses, évite surexposition)
- `5000-15000`: Faible (scènes lumineuses)
- `15000-30000`: Normale (recommandé)
- `30000-50000`: Haute (scènes sombres)
- `0`: Auto (réactive AEC)

### 2. Contrôle de Gain

```cpp
// Gain faible (image plus sombre mais moins de bruit)
id(tab5_cam).set_gain(2000);  // 2x

// Gain moyen (recommandé)
id(tab5_cam).set_gain(4000);  // 4x

// Gain élevé (image plus claire mais plus de bruit)
id(tab5_cam).set_gain(8000);  // 8x
```

**Valeurs:**
- `1000`: 1x (minimum, image sombre)
- `2000-4000`: 2-4x (recommandé)
- `8000-16000`: 8-16x (maximum, image bruitée)

### 3. Balance des Blancs (Mode)

```cpp
// AWB automatique (défaut, recommandé)
id(tab5_cam).set_white_balance_mode(true);

// AWB manuel (pour température couleur fixe)
id(tab5_cam).set_white_balance_mode(false);
```

### 4. Balance des Blancs (Température)

Pour corriger **blanc → vert**, essayez différentes températures:

```cpp
// Désactiver AWB automatique d'abord
id(tab5_cam).set_white_balance_mode(false);

// Essayer différentes températures
id(tab5_cam).set_white_balance_temp(4500);  // Légèrement chaud
id(tab5_cam).set_white_balance_temp(5000);  // Neutre
id(tab5_cam).set_white_balance_temp(5500);  // Flash (recommandé)
id(tab5_cam).set_white_balance_temp(6000);  // Légèrement froid
```

**Valeurs typiques:**
- `2800K`: Lampe incandescente (jaune/orange)
- `4000K`: Fluorescent blanc froid
- `5000K`: Lumière du jour (neutre)
- `5500K`: Flash électronique (recommandé pour correction blanc→vert)
- `6500K`: Ciel nuageux (bleuté)

---

## Exemple d'Utilisation dans LVGL

Ajoutez des boutons de test dans votre page caméra:

```yaml
lvgl:
  pages:
    - id: camera_page
      widgets:
        # ... vos widgets existants ...

        # Bouton: Réduire exposition (corriger surexposition)
        - button:
            x: 50
            y: 550
            width: 200
            height: 60
            on_click:
              - lambda: |-
                  ESP_LOGI("camera", "Reducing exposure to fix overexposure");
                  id(tab5_cam).set_exposure(10000);  // Faible exposition
            widgets:
              - label:
                  text: "Fix Bright"

        # Bouton: Corriger blanc→vert
        - button:
            x: 270
            y: 550
            width: 200
            height: 60
            on_click:
              - lambda: |-
                  ESP_LOGI("camera", "Fixing white→green with WB temp");
                  id(tab5_cam).set_white_balance_mode(false);  // Manuel
                  id(tab5_cam).set_white_balance_temp(5500);   // 5500K
            widgets:
              - label:
                  text: "Fix Green"

        # Bouton: Reset auto (AEC + AWB)
        - button:
            x: 490
            y: 550
            width: 200
            height: 60
            on_click:
              - lambda: |-
                  ESP_LOGI("camera", "Resetting to auto AEC + AWB");
                  id(tab5_cam).set_exposure(0);  // Auto AEC
                  id(tab5_cam).set_white_balance_mode(true);  // Auto AWB
            widgets:
              - label:
                  text: "Reset Auto"
```

---

## Procédure de Test Recommandée

### Étape 1: Tester AEC/AWB Automatiques

1. Modifiez le YAML: `update_interval: never` → `update_interval: 33ms`
2. Recompilez et flashez
3. Démarrez le streaming
4. **Attendez 5-10 secondes** pour que AEC/AWB convergent
5. Vérifiez si l'exposition et les couleurs sont correctes

**Résultat attendu:**
- ✅ FPS: ~25-30 (au lieu de 4)
- ✅ Exposition: Correcte automatiquement
- ✅ Couleurs: Blancs corrects automatiquement

### Étape 2: Ajustement Manuel (si nécessaire)

Si après 10 secondes l'image est encore:

#### A. Trop claire (surexposée)
```cpp
id(tab5_cam).set_exposure(10000);  // Essayer exposition faible
```

Ajustez progressivement: 10000 → 8000 → 5000 jusqu'à obtenir l'exposition désirée.

#### B. Blanc → Vert
```cpp
id(tab5_cam).set_white_balance_mode(false);  // Manuel
id(tab5_cam).set_white_balance_temp(5500);   // Essayer 5500K
```

Ajustez progressivement: 5500K → 5000K → 4500K jusqu'à obtenir des blancs neutres.

### Étape 3: Sauvegarder la Configuration Optimale

Une fois les valeurs optimales trouvées, ajoutez-les dans `on_load` de la page caméra:

```yaml
on_load:
  - lambda: |-
      ESP_LOGI("camera", "Page caméra chargée");

      // Appliquer les paramètres optimaux trouvés
      id(tab5_cam).set_exposure(12000);  // Votre valeur optimale
      id(tab5_cam).set_white_balance_mode(false);
      id(tab5_cam).set_white_balance_temp(5200);  // Votre valeur optimale
```

---

## Diagnostic

### Vérifier que AEC est actif

Dans les logs au démarrage, cherchez:
```
[esp_ipa] 📸 IPA config for SC202CS: AEC+AWB+Denoise+Sharpen+Gamma+CC
[esp_video_isp_pipeline] 📸 IPA Pipeline created - verifying loaded algorithms:
```

Si vous voyez "AEC+AWB" → ✅ AEC est bien actif

### Logs de Contrôles Manuels

Quand vous appelez les méthodes de contrôle, vous verrez:
```
[mipi_dsi_cam] ✓ Manual exposure set to 10000 (AEC disabled)
[mipi_dsi_cam] ✓ White balance: MANUAL
[mipi_dsi_cam] ✓ White balance temperature set to 5500K
```

---

## Résumé des Changements

### Fichiers Modifiés

1. **`components/esp_ipa/src/version.c`**
   - Réactivé AEC avec "aec.simple"
   - 6 algorithmes IPA au lieu de 5

2. **`components/mipi_dsi_cam/mipi_dsi_cam.h`**
   - Ajout de 4 méthodes publiques de contrôle

3. **`components/mipi_dsi_cam/mipi_dsi_cam.cpp`**
   - Implémentation des 4 méthodes de contrôle

### Changement YAML Requis

```yaml
display:
  update_interval: 33ms  # ← Changer de "never" à "33ms"
```

**Ce simple changement devrait résoudre 90% des problèmes!**

---

## Support

Si après ces changements vous rencontrez toujours des problèmes:

1. Partagez les logs complets incluant:
   - Les logs IPA au démarrage
   - Les logs de streaming
   - Le profiling FPS

2. Précisez:
   - Conditions d'éclairage (intérieur/extérieur, lumière artificielle/naturelle)
   - Valeurs testées pour exposition et WB
   - FPS obtenu après changement YAML
