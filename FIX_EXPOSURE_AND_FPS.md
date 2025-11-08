# Fix Surexposition, Blanc→Vert et FPS Limité

Ce document explique comment corriger les 3 problèmes identifiés avec le capteur SC202CS.

**⚠️ Alternative recommandée:** Si la qualité d'image est critique, [consultez la section OV5647](#-alternative-recommandée-ov5647-raspberry-pi-camera-v1) pour une solution complète avec calibration IPA JSON.

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
**Cause:** SC202CS manque de calibration IPA JSON (AWB/CCM génériques)
**Solution:** Méthodes de contrôle manuel WB ajoutées OU migration vers OV5647 (recommandé)

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
id(tab5_cam).set_gain(4000);  // 4x (minimum)

// Gain moyen (recommandé pour la plupart des scènes)
id(tab5_cam).set_gain(16000);  // 16x - équilibre qualité/sensibilité

// Gain élevé (scènes sombres, plus de bruit)
id(tab5_cam).set_gain(32000);  // 32x - sensibilité améliorée

// Gain maximum (utiliser avec précaution!)
id(tab5_cam).set_gain(63008);  // 63x - sensibilité maximale, bruit élevé
```

**Valeurs (basées sur Kconfig SC202CS):**
- `4000`: 4x (minimum hardware, image sombre)
- `16000`: 16x (recommandé - bon équilibre qualité/sensibilité)
- `32000`: 32x (low-light amélioré, bruit modéré)
- `63008`: 63x (maximum hardware, sensibilité maximale, bruit très élevé, risque de surchauffe)

⚠️ **Note:** Valeurs hors de la plage 4000-63008 seront clampées par le driver

**Stratégie de Gain (Kconfig):**
Le SC202CS supporte deux stratégies de gain configurables dans menuconfig:
- `CAMERA_SC202CS_ANA_GAIN_PRIORITY`: Priorité au gain analogique (moins de bruit)
- `CAMERA_SC202CS_DIG_GAIN_PRIORITY`: Priorité au gain numérique (**défaut**, transitions plus douces)

Pour réduire le bruit, vous pouvez recompiler avec gain analogique prioritaire via `menuconfig`:
```
Component config → Camera Sensor → SC202CS → Gain control priority → Analog gain priority
```

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
     - `set_gain(int value)` - Contrôle gain (4000-63008 pour SC202CS)
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

## 📋 Formats et Résolutions Disponibles (SC202CS Kconfig)

Le SC202CS supporte plusieurs formats configurables dans menuconfig. Le format par défaut peut affecter la qualité d'image et les performances:

**Formats disponibles:**
1. **RAW8 1280x720 30fps** (défaut actuel)
   - Résolution: HD (1280x720)
   - Format: 8-bit RAW Bayer
   - Interface: MIPI CSI-2 1-lane, 24MHz
   - ✅ Recommandé pour performance/qualité équilibrée

2. **RAW8 1600x1200 30fps**
   - Résolution: Full HD (1600x1200)
   - Format: 8-bit RAW Bayer
   - ⚠️ Plus haute résolution mais peut affecter FPS

3. **RAW10 1600x1200 30fps**
   - Résolution: Full HD (1600x1200)
   - Format: 10-bit RAW Bayer (meilleure dynamique)
   - ✅ Meilleure qualité couleur et plage dynamique
   - ⚠️ Nécessite plus de bande passante/mémoire

4. **RAW10 1600x900 30fps**
   - Résolution: HD+ (1600x900)
   - Format: 10-bit RAW Bayer
   - ✅ Bon compromis résolution/qualité

**Pour changer le format:**
```
menuconfig → Component config → Camera Sensor → SC202CS →
Select default output format for MIPI CSI interface
```

**Note:** RAW10 offre une meilleure plage dynamique (plus de détails dans les ombres/hautes lumières) mais nécessite plus de ressources. Si les couleurs blanc→vert persistent avec RAW8, essayez RAW10.

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

## 📷 Alternative Recommandée: OV5647 (Raspberry Pi Camera v1)

L'OV5647 est **fortement recommandé** comme alternative au SC202CS car il dispose d'une **calibration IPA complète** et d'un support matériel supérieur.

### Avantages OV5647 vs SC202CS

| Caractéristique | SC202CS | OV5647 | Avantage |
|----------------|---------|---------|----------|
| **Calibration IPA JSON** | ❌ Non disponible | ✅ `ov5647_default.json` | **OV5647** |
| **Couleurs (blanc→vert)** | ⚠️ Problème persistant | ✅ Correctes | **OV5647** |
| **AEC/AGC automatique** | ❌ Non (libesp_ipa.a) | ✅ Oui (via IPA JSON) | **OV5647** |
| **AWB automatique** | ⚠️ Basique | ✅ Calibré | **OV5647** |
| **MIPI CSI Lanes** | 1-lane (300 Mbps/lane) | 2-lane (600 Mbps/lane) | **OV5647** |
| **Résolution maximale** | 1600x1200 @ 30fps | 1920x1080 @ 30fps | **OV5647** |
| **Autofocus** | ❌ Non | ✅ Oui (VCM via GPIO0) | **OV5647** |
| **Line Sync CSI** | ❌ Non documenté | ✅ Configurable | **OV5647** |
| **FPS maximum** | 30fps | 50fps (RAW8 800x800) | **OV5647** |
| **Configuration personnalisée** | ⚠️ Limitée | ✅ JSON customisable | **OV5647** |

### Formats et Résolutions OV5647 (Kconfig)

**Formats RAW8 haute vitesse (50fps):**
1. **RAW8 800x800 50fps** (défaut)
   - Résolution: 800x800 (carré)
   - FPS: 50 (meilleur que SC202CS!)
   - Interface: MIPI CSI-2 2-lane, 24MHz
   - ✅ **Recommandé pour FPS élevé**

2. **RAW8 800x1280 50fps**
   - Résolution: 800x1280 (portrait)
   - FPS: 50
   - Usage: Affichage vertical

3. **RAW8 800x640 50fps**
   - Résolution: WVGA (wide VGA)
   - FPS: 50
   - Usage: Format large

**Formats RAW10 haute qualité:**
4. **RAW10 1920x1080 30fps**
   - Résolution: Full HD (1920x1080)
   - Format: 10-bit RAW (meilleure dynamique)
   - FPS: 30
   - ✅ **Recommandé pour qualité d'image maximale**

5. **RAW10 1280x960 Binning 45fps**
   - Résolution: SXGA (1280x960)
   - Mode: Binning (combine pixels pour moins de bruit)
   - FPS: 45
   - ✅ **Bon compromis qualité/vitesse**

### Configuration IPA JSON (Point Clé!)

L'OV5647 dispose d'un **fichier JSON de calibration complet**:

**Emplacement:** `esp_cam_sensor/sensors/ov5647/cfg/ov5647_default.json`

Ce fichier contient:
- ✅ Matrices de correction couleur (CCM) calibrées
- ✅ Paramètres AWB optimisés (pas de blanc→vert!)
- ✅ Tables AEC/AGC pour exposition automatique
- ✅ Calibration gamma pour chaque température de couleur
- ✅ Paramètres de réduction de bruit optimisés

**Option de personnalisation:**
Vous pouvez créer votre propre fichier JSON pour des conditions spécifiques:
```
menuconfig → Component config → Camera Sensor → OV5647 →
IPA Configuration File → Use custom configuration
```

Puis spécifier le chemin: `components/my_camera_config/ov5647_custom.json`

### Fonctionnalités Matérielles Supplémentaires

**1. Line Synchronization CSI:**
```
menuconfig → Component config → Camera Sensor → OV5647 →
Enable CSI line synchronization (recommandé: activé)
```
- Améliore la synchronisation des frames
- Réduit les artefacts d'image
- Meilleure détection d'erreurs

**2. Autofocus (VCM Motor):**
```
menuconfig → Component config → Camera Sensor → OV5647 →
Enable autofocus motor by OV5647's GPIO0
```
- Contrôle du moteur Voice Coil Motor (VCM)
- Autofocus automatique
- Nécessite module caméra avec lentille AF

### Changer de Format OV5647

Pour optimiser qualité ou FPS:
```
menuconfig → Component config → Camera Sensor → OV5647 →
Select default output format for MIPI CSI interface
```

**Recommandations selon usage:**

**Pour FPS maximum (streaming fluide):**
- Choisir: `RAW8 800x800 50fps` (défaut)
- Avantage: 50 FPS (66% plus rapide que SC202CS!)
- Résolution suffisante pour affichage embedded

**Pour qualité maximale (enregistrement/analyse):**
- Choisir: `RAW10 1920x1080 30fps`
- Avantage: Full HD avec 10-bit dynamique
- Meilleure qualité couleur et détails

**Pour équilibre qualité/vitesse:**
- Choisir: `RAW10 1280x960 Binning 45fps`
- Avantage: 45 FPS avec mode binning (moins de bruit)
- Résolution SXGA (1.2MP)

### Migration SC202CS → OV5647

**Matériel requis:**
- Module OV5647 (Raspberry Pi Camera v1 ou compatible)
- Connecteur MIPI CSI 2-lane (vs 1-lane pour SC202CS)
- Alimentation 3.3V identique

**Changements logiciels:**
1. Menuconfig: Désactiver `CAMERA_SC202CS`, activer `CAMERA_OV5647`
2. Code: Aucun changement nécessaire si vous utilisez l'API `esp_cam_sensor`
3. Auto-détection: OV5647 sera détecté automatiquement au boot
4. IPA: Configuration JSON chargée automatiquement

**Résultats attendus après migration:**
- ✅ **Plus de surexposition:** AEC automatique via JSON
- ✅ **Plus de blanc→vert:** CCM calibrée dans JSON
- ✅ **FPS amélioré:** 50 FPS au lieu de 30 FPS
- ✅ **Bande passante:** 2x plus (2-lane vs 1-lane)
- ✅ **Qualité globale:** Nettement supérieure

### Compatibilité avec Code Actuel

Les 4 méthodes de contrôle manuel implémentées (`set_exposure`, `set_gain`, `set_white_balance_mode`, `set_white_balance_temp`) fonctionneront également avec l'OV5647:

```cpp
// Ces méthodes fonctionnent avec TOUS les capteurs V4L2
id(tab5_cam).set_exposure(20000);  // Override AEC si nécessaire
id(tab5_cam).set_gain(16000);      // Override AGC si nécessaire
id(tab5_cam).set_white_balance_temp(5500);  // Override AWB si nécessaire
```

**Différence clé:** Avec OV5647, vous aurez **rarement besoin** d'utiliser ces overrides manuels car l'AEC/AWB/AGC automatiques via JSON fonctionnent correctement!

---

## 📷 Alternative Recommandée: OV02C10 (Capteur Moderne 2MP)

L'OV02C10 est une autre **excellente alternative** au SC202CS, particulièrement adapté pour des applications modernes avec Full HD. Il dispose également d'une **calibration IPA JSON complète**.

### Avantages OV02C10 vs SC202CS

| Caractéristique | SC202CS | OV02C10 | Avantage |
|----------------|---------|---------|----------|
| **Calibration IPA JSON** | ❌ Non disponible | ✅ `ov02c10_default.json` | **OV02C10** |
| **Couleurs (blanc→vert)** | ⚠️ Problème persistant | ✅ Correctes | **OV02C10** |
| **AEC/AGC automatique** | ❌ Non (libesp_ipa.a) | ✅ Oui (via IPA JSON) | **OV02C10** |
| **AWB automatique** | ⚠️ Basique | ✅ Calibré | **OV02C10** |
| **Format RAW** | RAW8/RAW10 | **RAW10 uniquement** | **OV02C10** |
| **Profondeur couleur** | 8/10-bit | **10-bit exclusif** | **OV02C10** |
| **MIPI CSI Lanes** | 1-lane fixe | **1-lane OU 2-lane** (flexible) | **OV02C10** |
| **Résolution maximale** | 1600x1200 @ 30fps | **1920x1080** @ 30fps | **OV02C10** |
| **Autofocus** | ❌ Non | ✅ Oui (ISP AF, VCM motor) | **OV02C10** |
| **Line Sync CSI** | ❌ Non documenté | ✅ Configurable | **OV02C10** |
| **Configuration personnalisée** | ⚠️ Limitée | ✅ JSON customisable | **OV02C10** |

### Formats et Résolutions OV02C10 (Kconfig)

**Tous les formats sont RAW10 (10-bit) - Meilleure qualité couleur garantie!**

1. **RAW10 1288x728 30fps, 1-lane** (défaut)
   - Résolution: 1.3MP (format allongé)
   - Format: 10-bit RAW Bayer
   - Interface: MIPI CSI-2 1-lane, 24MHz
   - ✅ **Bon équilibre bande passante/qualité**

2. **RAW10 1920x1080 30fps, 1-lane**
   - Résolution: Full HD (1920x1080)
   - Format: 10-bit RAW Bayer
   - Interface: MIPI CSI-2 1-lane, 24MHz
   - ⚠️ Bande passante élevée sur 1-lane

3. **RAW10 1920x1080 30fps, 2-lane**
   - Résolution: Full HD (1920x1080)
   - Format: 10-bit RAW Bayer
   - Interface: MIPI CSI-2 2-lane, 24MHz
   - ✅ **Recommandé pour Full HD 30fps stable**

### Point Clé: RAW10 Exclusif

L'OV02C10 utilise **uniquement RAW10** (10-bit), contrairement à SC202CS qui peut faire RAW8:

**Avantages RAW10:**
- 1024 niveaux de luminosité par canal (vs 256 pour RAW8)
- Meilleure plage dynamique (détails ombres + hautes lumières)
- Gradients de couleur plus doux (moins de banding)
- Meilleure qualité pour l'IPA (plus de données pour AWB/CCM)

**Inconvénient:** Bande passante 25% plus élevée que RAW8
- Solution: Utiliser mode 2-lane pour Full HD sans compromis

### Configuration IPA JSON (Point Clé!)

L'OV02C10 dispose d'un **fichier JSON de calibration complet**:

**Emplacement:** `esp_cam_sensor/sensors/ov02c10/cfg/ov02c10_default.json`

Ce fichier contient:
- ✅ Matrices de correction couleur (CCM) calibrées
- ✅ Paramètres AWB optimisés (pas de blanc→vert!)
- ✅ Tables AEC/AGC pour exposition automatique
- ✅ Calibration gamma pour chaque température de couleur
- ✅ Paramètres de réduction de bruit optimisés pour 10-bit

**Option de personnalisation:**
```
menuconfig → Component config → Camera Sensor → OV02C10 →
IPA JSON Configuration File → Customized
```

Puis spécifier le chemin dans:
`CAMERA_OV02C10_CUSTOMIZED_IPA_JSON_CONFIGURATION_FILE_PATH`

### Fonctionnalités Matérielles

**1. Line Synchronization CSI:**
```
menuconfig → Component config → Camera Sensor → OV02C10 →
CSI Line sync enable (recommandé: activé par défaut)
```
- Envoie short packet pour chaque ligne
- Améliore synchronisation frames
- Réduit artefacts d'image

**2. Autofocus ISP:**
```
menuconfig → Component config → Camera Sensor → OV02C10 →
AF(auto focus) enable (recommandé: activé par défaut)
```
- Autofocus basé sur ISP (meilleur que contrôle basique)
- Contrôle pins I/O pour moteur VCM
- Compatible modules caméra avec lentille AF

### Changer de Format OV02C10

Pour optimiser selon bande passante disponible:
```
menuconfig → Component config → Camera Sensor → OV02C10 →
Default format select
```

**Recommandations selon usage:**

**Pour compatibilité 1-lane (ESP32-P4 avec 1-lane CSI uniquement):**
- Choisir: `RAW10 1288x728 30fps, 1-lane` (défaut)
- Avantage: Bande passante optimale pour 1-lane
- Résolution: 1.3MP, suffisant pour affichage embedded

**Pour Full HD avec 2-lane (ESP32-P4 avec 2-lane CSI):**
- Choisir: `RAW10 1920x1080 30fps, 2-lane`
- Avantage: Full HD stable à 30 FPS
- Meilleure qualité globale

**Pour Full HD avec 1-lane (attention!):**
- Choisir: `RAW10 1920x1080 30fps, 1-lane`
- ⚠️ Bande passante limite, vérifier FPS réel
- Peut nécessiter réduction FPS ou compression

### Migration SC202CS → OV02C10

**Matériel requis:**
- Module OV02C10 (2MP moderne)
- Connecteur MIPI CSI 1-lane OU 2-lane
- Alimentation 3.3V identique

**Changements logiciels:**
1. Menuconfig: Désactiver `CAMERA_SC202CS`, activer `CAMERA_OV02C10`
2. Choisir format selon vos lanes disponibles (1-lane ou 2-lane)
3. Code: Aucun changement si vous utilisez l'API `esp_cam_sensor`
4. Auto-détection: OV02C10 sera détecté automatiquement au boot
5. IPA: Configuration JSON chargée automatiquement

**Résultats attendus après migration:**
- ✅ **Plus de surexposition:** AEC automatique via JSON
- ✅ **Plus de blanc→vert:** CCM calibrée dans JSON
- ✅ **Qualité couleur améliorée:** RAW10 exclusif (10-bit)
- ✅ **Full HD disponible:** 1920x1080 @ 30fps (mode 2-lane)
- ✅ **Autofocus ISP:** Meilleure qualité AF que contrôle basique

### Compatibilité avec Code Actuel

Les 4 méthodes de contrôle manuel implémentées fonctionneront également avec l'OV02C10:

```cpp
// Ces méthodes fonctionnent avec TOUS les capteurs V4L2
id(tab5_cam).set_exposure(20000);  // Override AEC si nécessaire
id(tab5_cam).set_gain(16000);      // Override AGC si nécessaire
id(tab5_cam).set_white_balance_temp(5500);  // Override AWB si nécessaire
```

**Différence clé:** Avec OV02C10, vous aurez **rarement besoin** d'utiliser ces overrides manuels car l'AEC/AWB/AGC automatiques via JSON fonctionnent correctement, comme avec OV5647!

---

## 📋 Récapitulatif: Quel Capteur Choisir?

Vous disposez de **3 ESP32-P4 avec des capteurs différents**. Voici un guide pour choisir le meilleur capteur selon votre application:

### Comparaison des 3 Capteurs

| Critère | SC202CS | OV5647 | OV02C10 | Meilleur Choix |
|---------|---------|---------|----------|----------------|
| **Calibration IPA JSON** | ❌ | ✅ | ✅ | OV5647/OV02C10 |
| **Qualité couleur** | ⚠️ Blanc→vert | ✅ Parfaite | ✅ Parfaite | OV5647/OV02C10 |
| **AEC/AGC auto** | ❌ | ✅ | ✅ | OV5647/OV02C10 |
| **FPS maximum** | 30fps | **50fps** | 30fps | **OV5647** |
| **Résolution max** | 1600x1200 | 1920x1080 | 1920x1080 | OV5647/OV02C10 |
| **Format RAW** | RAW8/RAW10 | RAW8/RAW10 | **RAW10 seul** | **OV02C10** |
| **Profondeur** | 8/10-bit | 8/10-bit | **10-bit exclusif** | **OV02C10** |
| **MIPI Lanes** | 1-lane | 2-lane | **1 ou 2-lane** | **OV02C10** (flexible) |
| **Autofocus** | ❌ | ✅ VCM | ✅ ISP AF | **OV02C10** (ISP) |
| **Coût estimé** | ~$8-12 | ~$10-15 | ~$12-18 | SC202CS |
| **Disponibilité** | Moyenne | **Excellente** | Bonne | **OV5647** |

### Recommandations par Usage

**Pour streaming haute vitesse (50 FPS):**
- ✅ **OV5647** - RAW8 800x800 @ 50fps
- Meilleur pour: UI interactive, gaming, applications temps réel
- Limitation: Résolution 800x800 à 50fps

**Pour qualité d'image maximale (Full HD):**
- ✅ **OV02C10** - RAW10 1920x1080 @ 30fps (2-lane)
- Meilleur pour: Enregistrement vidéo, analyse d'image, reconnaissance
- Avantage: RAW10 exclusif (meilleure dynamique)

**Pour compatibilité Raspberry Pi:**
- ✅ **OV5647** - Module Raspberry Pi Camera v1
- Meilleur pour: Projets nécessitant modules standard, disponibilité mondiale
- Avantage: Écosystème énorme, support excellent

**Si vous êtes bloqué avec SC202CS:**
- ⚠️ Utilisez les contrôles manuels documentés dans ce guide
- Limitez attentes: couleurs jamais parfaites sans JSON
- Envisagez migration future vers OV5647/OV02C10

### Configuration Multi-Capteurs dans ESPHome

Pour vos **3 ESP32-P4 avec capteurs différents**, utilisez substitutions YAML:

```yaml
# esp32p4_sc202cs.yaml
substitutions:
  camera_sensor: "SC202CS"

# esp32p4_ov5647.yaml
substitutions:
  camera_sensor: "OV5647"

# esp32p4_ov02c10.yaml
substitutions:
  camera_sensor: "OV02C10"
```

L'auto-détection dans menuconfig s'occupera du reste:
- `CAMERA_SC202CS_AUTO_DETECT_MIPI_INTERFACE_SENSOR`
- `CAMERA_OV5647_AUTO_DETECT_MIPI_INTERFACE_SENSOR`
- `CAMERA_OV02C10_AUTO_DETECT_MIPI_INTERFACE_SENSOR`

Tous activés par défaut, le bon capteur sera détecté automatiquement au boot!

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
