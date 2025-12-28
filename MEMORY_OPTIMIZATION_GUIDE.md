# 💾 Guide d'Optimisation Mémoire - ESP32-P4

## 🔴 Problème : Dépassement de Mémoire

**Capacité disponible**: 16 Mo RAM
**Utilisation actuelle**:
- `face_detection`: ~4.8 Mo (30%)
- `yolo11_detection`: >6.4 Mo (>40%) ❌ **DÉPASSEMENT**
- `pedestrian_detection`: ~4-5 Mo estimé

**Total si tous actifs**: >15 Mo → **IMPOSSIBLE**

---

## ✅ Solution 1: N'utiliser QU'UN SEUL Détecteur à la Fois ⭐ RECOMMANDÉ

### Option A: Reconnaissance Faciale Uniquement

```yaml
# Configuration YAML
face_detection:
  id: face_detector
  camera_id: tab5_cam
  detection_interval: 8
  recognition_enabled: true
  face_db_path: "/sdcard/faces.db"
  draw_enabled: true

# COMMENTEZ les autres:
# yolo11_detection:
#   ...

# pedestrian_detection:
#   ...
```

**Mémoire utilisée**: ~5 Mo (30%)
**Disponible**: ~11 Mo pour l'application ✅

---

### Option B: YOLO11 Détection d'Objets

```yaml
# Configuration YAML
yolo11_detection:
  id: yolo11_detector
  camera_id: tab5_cam
  detection_interval: 10
  score_threshold: 0.4
  draw_enabled: true

# COMMENTEZ face_detection et pedestrian_detection
```

**Mémoire utilisée**: ~6.5 Mo (40%)
**Disponible**: ~9.5 Mo pour l'application ✅

---

### Option C: Détection de Piétons

```yaml
# Configuration YAML
pedestrian_detection:
  id: pedestrian_detector
  camera_id: tab5_cam
  detection_interval: 6
  draw_enabled: true

# COMMENTEZ face_detection et yolo11_detection
```

**Mémoire utilisée**: ~4-5 Mo (25-30%)
**Disponible**: ~11-12 Mo pour l'application ✅

---

## ✅ Solution 2: Système de Commutation Dynamique

Charger/décharger les modèles à la demande (nécessite redémarrage du composant).

### Architecture Recommandée

```
Page 1: Face Recognition
  └─ Charge uniquement face_detection

Page 2: YOLO11 Détection
  └─ Charge uniquement yolo11_detection

Page 3: Pedestrian Detection
  └─ Charge uniquement pedestrian_detection
```

### Implémentation

Créer plusieurs fichiers YAML de configuration:

**1. `face_detection_mode.yaml`** - Face unlock / reconnaissance
```yaml
face_detection:
  id: face_detector
  camera_id: tab5_cam
  recognition_enabled: true
  detection_interval: 8

# yolo11_detection: DÉSACTIVÉ
# pedestrian_detection: DÉSACTIVÉ
```

**2. `yolo11_mode.yaml`** - Détection d'objets
```yaml
yolo11_detection:
  id: yolo11_detector
  camera_id: tab5_cam
  detection_interval: 10

# face_detection: DÉSACTIVÉ
# pedestrian_detection: DÉSACTIVÉ
```

**3. `pedestrian_mode.yaml`** - Détection de piétons
```yaml
pedestrian_detection:
  id: pedestrian_detector
  camera_id: tab5_cam
  detection_interval: 6

# face_detection: DÉSACTIVÉ
# yolo11_detection: DÉSACTIVÉ
```

Compiler avec le mode souhaité en utilisant `packages:` dans votre config principale.

---

## ✅ Solution 3: Réduire la Taille des Modèles

### Modèles Quantifiés INT8

Les modèles actuels utilisent déjà INT8 (S8):
- `HUMAN_FACE_DETECT_MSRMNP_S8_V1` ✅
- `YOLO11_DETECT_S8_V1` ✅
- `PEDESTRIAN_DETECT_PICO_S8_V1` ✅

**Pas d'optimisation possible ici** - déjà au minimum.

### Modèles Plus Petits

Vérifier si ESP-DL propose des versions "nano" ou "pico":
- `face_detect_nano.espdl` (si disponible)
- `yolo11n.espdl` (YOLO11 Nano - plus petit)
- `pedestrian_pico.espdl` (déjà utilisé)

---

## ✅ Solution 4: Stockage Flash au lieu de RAM

### Configuration Actuelle (RAM)

```python
# __init__.py - CHARGEMENT EN RAM
cg.add_build_flag("-DCONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA=1")
```

Tous les modèles sont actuellement configurés avec:
- `MODEL_IN_FLASH_RODATA=1`

**Vérifier** que les modèles sont bien en Flash et non chargés en RAM.

### Vérification

Regarder les logs au démarrage:
```
[face_detection] Model loaded from: FLASH
```

Si le modèle est en RAM, changer:
```python
# Dans __init__.py
cg.add_build_flag("-DCONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA=1")
# Devient
cg.add_build_flag("-DCONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_XIP=1")
```

---

## 📊 Comparaison des Solutions

| Solution | Mémoire Dispo | Complexité | Flexibilité |
|----------|---------------|------------|-------------|
| 1 seul modèle | ~10-11 Mo | ⭐ Facile | ❌ Limitée |
| Commutation | ~10-11 Mo | ⭐⭐ Moyenne | ✅ Bonne |
| Modèles nano | ~12-13 Mo | ⭐⭐⭐ Difficile | ✅ Excellente |
| Flash XIP | ~14-15 Mo | ⭐⭐ Moyenne | ✅ Excellente |

---

## 🎯 Recommandation Finale

### Pour votre cas d'usage:

**Si vous avez besoin de face unlock:**
```yaml
# Configuration finale recommandée
face_detection:
  id: face_detector
  camera_id: tab5_cam
  recognition_enabled: true
  detection_interval: 10        # Optimisé
  score_threshold: 0.35
  recognition_threshold: 0.70
  draw_enabled: true
```

**Si vous avez besoin de détection d'objets:**
```yaml
# Alternative
yolo11_detection:
  id: yolo11_detector
  camera_id: tab5_cam
  detection_interval: 12        # Optimisé
  score_threshold: 0.45         # Plus strict = moins de mémoire cache
  draw_enabled: true
```

**❌ NE PAS activer plusieurs détections simultanément**

---

## 🔍 Diagnostic Mémoire

### Ajouter au code pour surveiller la mémoire:

```yaml
# Dans votre configuration
interval:
  - interval: 30s
    then:
      - lambda: |-
          ESP_LOGI("memory", "Free heap: %u bytes (%.1f%%)",
                   esp_get_free_heap_size(),
                   (float)esp_get_free_heap_size() / (16*1024*1024) * 100);
```

Cela affichera la mémoire disponible toutes les 30 secondes.

---

## 📝 Prochaines Étapes

1. ✅ **Choisir UN seul détecteur** selon votre besoin principal
2. ✅ Commenter les autres détecteurs dans votre YAML
3. ✅ Recompiler avec `esphome compile`
4. ✅ Vérifier les logs mémoire au démarrage
5. ✅ Si besoin de plusieurs modes, créer plusieurs configs YAML

---

## ⚠️ Points Importants

- **ESP32-P4 a 16 Mo de RAM totale**, mais:
  - ~2-3 Mo réservés pour le système
  - ~2-3 Mo pour LVGL et framebuffers
  - **~10-11 Mo disponibles** pour les applications

- Les modèles ESP-DL sont volumineux:
  - Face detection: 4-5 Mo
  - YOLO11: 6-7 Mo
  - Pedestrian: 4-5 Mo

- **Impossible de tout charger en même temps** ❌

---

## 🚀 Conclusion

**Votre ESP32-P4 n'a physiquement pas assez de mémoire pour 3 détecteurs simultanés.**

Choisissez votre priorité:
1. 🔐 **Face unlock** → `face_detection` uniquement
2. 🎯 **Détection d'objets** → `yolo11_detection` uniquement
3. 🚶 **Détection piétons** → `pedestrian_detection` uniquement

**La correction précédente pour `pedestrian_detection` reste valide** - elle empêche les erreurs de compilation si le modèle n'est pas chargé.
