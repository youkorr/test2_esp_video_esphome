# 🎯 Guide d'Optimisation - Détection ESP32-P4

## 📊 Analyse des Problèmes

### 1. **face_detection** - 30% CPU ⚠️
- **Status**: Fonctionne mais gourmand
- **Intervalle défaut**: 8 frames
- **Charge**: 2 modèles (détection + reconnaissance)
- **Fichier**: `components/face_detection/face_detection.cpp`

### 2. **yolo11_detection** - 40%+ CPU ⚠️⚠️
- **Status**: Fonctionne mais très gourmand
- **Intervalle défaut**: 8 frames
- **Charge**: Modèle YOLO11 complexe (détection multi-objets)
- **Fichier**: `components/yolo11_detection/yolo11_detection.cpp`

### 3. **pedestrian_detection** - NE FONCTIONNE PAS ❌
- **Status**: **CORRIGÉ** dans ce commit
- **Problème**: Absence de protections conditionnelles `#ifdef`
- **Solution**: Ajout de `#ifdef ESP_DL_MODEL_PEDESTRIAN`

---

## ✅ Corrections Appliquées

### pedestrian_detection - Protection Conditionnelle

**Problème identifié**:
```cpp
// AVANT (ERREUR)
#include "pedestrian_detect.hpp"  // Pas de #ifdef !
```

**Solution appliquée**:
```cpp
// APRÈS (CORRIGÉ)
#ifdef ESP_DL_MODEL_PEDESTRIAN
#include "pedestrian_detect.hpp"
#include "dl_image.hpp"
#endif
```

**Fichiers modifiés**:
- `components/pedestrian_detection/pedestrian_detection.cpp` (4 sections)
- `components/pedestrian_detection/__init__.py` (ajout flag `-DESP_DL_MODEL_PEDESTRIAN=1`)

---

## 🔧 Recommandations d'Optimisation

### Option 1: Augmenter les Intervalles de Détection ⭐ RECOMMANDÉ

Réduire la fréquence de détection pour libérer du CPU:

```yaml
# Configuration optimisée pour ESP32-P4
face_detection:
  id: face_detector
  camera_id: tab5_cam
  detection_interval: 15    # ↑ De 8 à 15 frames (-47% CPU)
  score_threshold: 0.3
  nms_threshold: 0.5
  draw_enabled: true

yolo11_detection:
  id: yolo11_detector
  camera_id: tab5_cam
  detection_interval: 20    # ↑ De 8 à 20 frames (-60% CPU)
  score_threshold: 0.3
  nms_threshold: 0.5
  draw_enabled: true

pedestrian_detection:
  id: pedestrian_detector
  camera_id: tab5_cam
  detection_interval: 12    # ↑ De 4 à 12 frames (-67% CPU)
  score_threshold: 0.5
  nms_threshold: 0.5
  draw_enabled: true
```

**Impact CPU estimé**:
- face_detection: 30% → ~16% (-14%)
- yolo11_detection: 40% → ~16% (-24%)
- pedestrian_detection: ~25% → ~8% (-17%)
- **Total gain**: ~55% CPU disponible

---

### Option 2: Utiliser 1 Seul Détecteur à la Fois

Pour les applications qui n'ont pas besoin de détections simultanées:

```yaml
# CHOIX 1: Détection de visages uniquement
face_detection:
  id: face_detector
  camera_id: tab5_cam
  detection_interval: 8
  recognition_enabled: true

# CHOIX 2: Détection d'objets YOLO11
# yolo11_detection:
#   id: yolo11_detector
#   camera_id: tab5_cam
#   detection_interval: 10

# CHOIX 3: Détection de piétons
# pedestrian_detection:
#   id: pedestrian_detector
#   camera_id: tab5_cam
#   detection_interval: 6
```

**Impact CPU**:
- 1 seul modèle actif = 15-25% CPU maximum
- **Gain**: 45-55% CPU disponible

---

### Option 3: Désactiver le Dessin des Boîtes

Si vous n'avez pas besoin d'affichage visuel:

```yaml
yolo11_detection:
  id: yolo11_detector
  camera_id: tab5_cam
  detection_interval: 8
  draw_enabled: false        # ← Désactive le dessin (-5-10% CPU)
```

**Impact**: -5 à -10% CPU par détecteur

---

### Option 4: Activer/Désactiver Dynamiquement

Utiliser des boutons pour activer les détections à la demande:

```yaml
# Exemple de bouton toggle dans LVGL
- button:
    id: btn_yolo11_toggle
    on_click:
      then:
        - lambda: |-
            static bool enabled = true;
            enabled = !enabled;
            id(yolo11_detector).set_draw_enabled(enabled);
            if (enabled) {
              ESP_LOGI("yolo11", "ENABLED");
            } else {
              ESP_LOGI("yolo11", "DISABLED");
            }
```

Voir `LVGL_CAMERA_PAGE_OV02C10.yaml:160-191` pour l'exemple complet.

---

## 📝 Configuration Recommandée pour ESP32-P4

### Configuration Équilibrée (Tous Actifs)

```yaml
face_detection:
  id: face_detector
  camera_id: tab5_cam
  detection_interval: 15
  score_threshold: 0.35
  recognition_enabled: false    # Désactiver si non utilisé

yolo11_detection:
  id: yolo11_detector
  camera_id: tab5_cam
  detection_interval: 20
  score_threshold: 0.4          # Augmenter pour réduire faux positifs
  draw_enabled: true

pedestrian_detection:
  id: pedestrian_detector
  camera_id: tab5_cam
  detection_interval: 12
  score_threshold: 0.55
  draw_enabled: true
```

**Charge CPU estimée**: ~40-50% total
**Disponible pour autre traitement**: ~50-60%

---

## 🚀 Pour Aller Plus Loin

### 1. Utiliser la Résolution PPA

Réduire la résolution du flux pour la détection:

```yaml
mipi_dsi_cam:
  id: tab5_cam
  sensor_type: ov02c10
  resolution: "800x480"
  pixel_format: RGB565

  # Réduction matérielle pour détection
  output_width: 640
  output_height: 360         # 16:9 ratio maintenu
```

### 2. Ajuster les Seuils de Confiance

```yaml
# Score threshold élevé = moins de détections = moins de CPU
yolo11_detection:
  score_threshold: 0.5        # Par défaut: 0.3
  nms_threshold: 0.6          # Par défaut: 0.5
```

### 3. Priorités FreeRTOS

Ajuster les priorités des tâches si nécessaire (configuration avancée).

---

## 📚 Références

### Fichiers Principaux

- **face_detection**:
  - `components/face_detection/face_detection.cpp`
  - `components/face_detection/__init__.py`

- **yolo11_detection**:
  - `components/yolo11_detection/yolo11_detection.cpp`
  - `components/yolo11_detection/__init__.py`

- **pedestrian_detection**:
  - `components/pedestrian_detection/pedestrian_detection.cpp`
  - `components/pedestrian_detection/__init__.py`

### Exemples de Configuration

- Page LVGL avec toggle YOLO11: `LVGL_CAMERA_PAGE_OV02C10.yaml`
- Face unlock: `face_unlock_page.yaml`

---

## ⚡ Résumé des Actions

1. ✅ **pedestrian_detection** corrigé avec protections conditionnelles
2. 📈 Augmenter `detection_interval` de 8→15-20 frames
3. 🎯 Utiliser 1-2 détecteurs maximum simultanément
4. 🖼️ Désactiver `draw_enabled` si non nécessaire
5. 📏 Réduire résolution avec PPA si possible

**Résultat attendu**: Charge CPU totale < 50%, ESP32-P4 stable et réactif.
