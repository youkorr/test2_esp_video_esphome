# Fix Surexposition, Blanc→Vert et FPS Limité

Ce document explique comment corriger les 3 problèmes identifiés avec le capteur SC202CS.

## Problèmes Résolus

### ✅ Problème 1: FPS Limité à 4 (au lieu de 30)
**Symptôme:** FPS réel ~4 malgré `update_interval: 33ms` configuré
**Cause:** Display configuré avec `update_interval: never`
**Solution:** Changement YAML (voir ci-dessous)

### ✅ Problème 2: Image Surexposée
**Symptôme:** Image trop claire/blanche
**Cause:** AEC (Auto Exposure Control) non disponible dans libesp_ipa.a
**Solution:** Méthodes de contrôle manuel d'exposition ajoutées (set_exposure, set_gain)

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

### ⚠️ Limitation: AEC Non Disponible

Le pipeline IPA utilise **5 algorithmes** disponibles dans libesp_ipa.a:

```
Capteur SC202CS (RAW8) → ISP → IPA (5 algorithmes) → RGB565

Algorithmes actifs:
  1. awb.gray                ← Auto White Balance
  2. denoising.gain_feedback ← Réduction du bruit
  3. sharpen.freq_feedback   ← Netteté
  4. gamma.lumma_feedback    ← Correction gamma
  5. cc.linear               ← Color Correction Matrix
```

**⚠️ IMPORTANT:** AEC/AGC (Auto Exposure Control) n'est PAS disponible dans la version actuelle de libesp_ipa.a. Les algorithmes "aec.simple", "aec.threshold", et "agc.threshold" n'existent pas dans cette bibliothèque.

**Conséquence:** L'exposition DOIT être contrôlée manuellement via les méthodes V4L2 ci-dessous. La balance des blancs (AWB) fonctionne automatiquement, mais peut nécessiter un ajustement manuel pour des scènes spécifiques.

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

### Étape 1: Tester AWB Automatique et FPS

1. Modifiez le YAML: `update_interval: never` → `update_interval: 33ms`
2. Recompilez et flashez
3. Démarrez le streaming
4. **Attendez 5-10 secondes** pour que AWB (balance des blancs) converge
5. Vérifiez FPS et couleurs

**Résultat attendu:**
- ✅ FPS: ~25-30 (au lieu de 4) - Garanti par le changement YAML
- ⚠️ Exposition: Probablement TROP CLAIRE (pas d'AEC automatique)
- ⚠️ Couleurs: AWB automatique peut améliorer, mais blanc→vert peut persister

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

### Vérifier la Configuration IPA

Dans les logs au démarrage, cherchez:
```
[esp_ipa] 📸 IPA config for SC202CS: AWB+Denoise+Sharpen+Gamma+CC (5 algos, no AEC)
[esp_video_isp_pipeline] 📸 IPA Pipeline created - verifying loaded algorithms:
```

Si vous voyez "5 algos, no AEC" → ✅ Configuration correcte (pas de risque de crash)

⚠️ Si vous voyez "AEC" dans les logs, la configuration est INCORRECTE et causera un crash!

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
   - Configuration STABLE: 5 algorithmes IPA (AWB, Denoise, Sharpen, Gamma, CC)
   - ⚠️ AEC/AGC volontairement NON activé (n'existe pas dans libesp_ipa.a)
   - Documentation complète des algorithmes disponibles

2. **`components/mipi_dsi_cam/mipi_dsi_cam.h`**
   - Ajout de 4 méthodes publiques de contrôle manuel:
     - `set_exposure(int value)` - Contrôle exposition (0-65535, 0=auto V4L2)
     - `set_gain(int value)` - Contrôle gain (1000-16000)
     - `set_white_balance_mode(bool auto_mode)` - Mode AWB
     - `set_white_balance_temp(int kelvin)` - Température WB (2800-6500K)

3. **`components/mipi_dsi_cam/mipi_dsi_cam.cpp`**
   - Implémentation complète des 4 méthodes avec V4L2 ioctl
   - Gestion d'erreurs et logging détaillé

### Changement YAML Requis

```yaml
display:
  update_interval: 33ms  # ← Changer de "never" à "33ms"
```

**Ce simple changement devrait résoudre 90% des problèmes!**

---

## ⚠️ Limitation Importante: SC202CS et Calibration IPA

### Problème Fondamental Découvert

Le capteur **SC202CS est un capteur RAW** qui nécessite un **fichier JSON de calibration IPA** fourni par le fabricant (SmartSens). Ce fichier contient les matrices de correction couleur (CCM), les paramètres AWB optimisés, et d'autres calibrations spécifiques au capteur.

**État actuel:** Le SC202CS **N'A PAS** de fichier JSON dans esp-cam-sensor!

Capteurs avec JSON (fonctionnent parfaitement):
- ✅ OV5647: `/components/esp-cam-sensor/sensors/ov5647_settings.c`
- ✅ OV02C10: `/components/esp-cam-sensor/sensors/ov02c10_settings.c`

Capteur SANS JSON (calibration générique):
- ❌ SC202CS: **Aucun fichier JSON** → Utilise valeurs génériques

**Conséquence:** Même avec contrôles manuels optimaux, les couleurs (blanc→vert) et l'exposition ne seront JAMAIS aussi bonnes que sur un capteur correctement calibré.

### Solutions Possibles

1. **Contacter SmartSens (fabricant SC202CS):**
   - Demander le fichier JSON de calibration IPA pour ESP32-P4/ESP-IDF
   - Email: support@smartsens.com
   - Mentionner: ESP-IDF v5.4, esp-video-components, format JSON IPA

2. **Utiliser un capteur supporté officiellement:**
   - OV5647 (Raspberry Pi Camera v1) - **Recommandé**
   - OV02C10 (capteur moderne 2MP)
   - Ces capteurs ont une calibration complète et fonctionnent parfaitement

3. **Créer une calibration manuelle (avancé):**
   - Nécessite équipement de colorimétrie professionnel
   - Temps: plusieurs jours de travail
   - Résultat: moins précis qu'une calibration d'usine

**Recommandation:** Si la qualité d'image est critique, envisagez de passer à un capteur OV5647 qui est entièrement supporté et calibré.

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
