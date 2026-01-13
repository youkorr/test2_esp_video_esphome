# 🚀 Plan d'Intégration LVGL v9.4 Local

## Vue d'Ensemble

Ce document décrit le plan pour intégrer LVGL v9.4 **localement** dans ce dépôt, avec ThorVG/SVG/Lottie inclus, sans dépendre du fork clydebarrow/esphome.

---

## 🎯 Objectifs

1. **Indépendance** : Ne plus dépendre d'external_components Git externe
2. **Contrôle** : Maîtriser la version LVGL et les flags de compilation
3. **ThorVG** : Support complet SVG/Lottie intégré
4. **Compatibilité** : Fonctionner avec ESPHome stable
5. **Maintenance** : Faciliter les mises à jour LVGL

---

## 🏗️ Architecture Proposée

### Structure des Composants

```
components/
├── lvgl/                          # ← NOUVEAU COMPOSANT
│   ├── __init__.py               # Configuration ESPHome + lib_deps
│   ├── lvgl_esphome.h            # Interface C++ ESPHome
│   ├── lvgl_esphome.cpp          # Implémentation
│   ├── lv_conf.h                 # Configuration LVGL (ThorVG activé)
│   └── README.md                 # Documentation
│
├── storage/                       # ← EXISTANT (à adapter)
│   ├── __init__.py               # Retirer les flags ThorVG (délégué à lvgl/)
│   ├── storage.h
│   └── storage.cpp
│
└── lvgl_advanced_features/        # ← EXISTANT (peut être fusionné dans lvgl/)
    ├── __init__.py
    └── ...
```

---

## 📦 Phase 1 : Créer le Composant LVGL Local

### 1.1 - Créer `components/lvgl/__init__.py`

**Rôle** :
- Configurer LVGL via PlatformIO lib_deps
- Ajouter les flags de compilation pour ThorVG
- Définir le schéma YAML pour la configuration

**Contenu clé** :

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.const import *

# Namespace C++
lvgl_ns = cg.esphome_ns.namespace("lvgl")

# Configuration YAML
CONF_THORVG = "thorvg"
CONF_THORVG_INTERNAL = "internal"
CONF_THORVG_EXTERNAL = "external"
CONF_SVG = "svg"
CONF_LOTTIE = "lottie"
CONF_GIF = "gif"
CONF_BMP = "bmp"
CONF_LIBPNG = "png"

CONFIG_SCHEMA = cv.Schema({
    cv.Optional(CONF_THORVG): cv.Schema({
        cv.Optional(CONF_THORVG_INTERNAL, default=True): cv.boolean,
        cv.Optional(CONF_THORVG_EXTERNAL, default=False): cv.boolean,
    }),
    cv.Optional(CONF_SVG, default=False): cv.boolean,
    cv.Optional(CONF_LOTTIE, default=False): cv.boolean,
    cv.Optional(CONF_GIF, default=False): cv.boolean,
    cv.Optional(CONF_BMP, default=False): cv.boolean,
    cv.Optional(CONF_LIBPNG, default=False): cv.boolean,
})

async def to_code(config):
    # Ajouter LVGL v9.4.0 via PlatformIO
    cg.add_library("lvgl/lvgl", "^9.4.0")

    # Flags de base LVGL v9
    cg.add_build_flag("-DLV_CONF_INCLUDE_SIMPLE")
    cg.add_build_flag("-DLV_CONF_PATH=lv_conf.h")

    # ThorVG (si activé)
    if CONF_THORVG in config:
        thorvg_cfg = config[CONF_THORVG]
        if thorvg_cfg.get(CONF_THORVG_INTERNAL, False):
            cg.add_build_flag("-DLV_USE_THORVG_INTERNAL=1")
        if thorvg_cfg.get(CONF_THORVG_EXTERNAL, False):
            cg.add_build_flag("-DLV_USE_THORVG_EXTERNAL=1")

    # Décodeurs d'images
    if config.get(CONF_SVG, False):
        cg.add_build_flag("-DLV_USE_SVG=1")

    if config.get(CONF_LOTTIE, False):
        cg.add_build_flag("-DLV_USE_LOTTIE=1")

    if config.get(CONF_GIF, False):
        cg.add_build_flag("-DLV_USE_GIF=1")

    if config.get(CONF_BMP, False):
        cg.add_build_flag("-DLV_USE_BMP=1")

    if config.get(CONF_LIBPNG, False):
        cg.add_build_flag("-DLV_USE_LIBPNG=1")
        cg.add_library("pngdec", "1.0.1")  # Librairie PNG
```

### 1.2 - Créer `components/lvgl/lv_conf.h`

**Rôle** : Configuration LVGL adaptée pour ESP32-P4 avec ThorVG

**Contenu clé** :

```c
#ifndef LV_CONF_H
#define LV_CONF_H

// ==========================================
// CONFIGURATION LVGL v9.4 POUR ESP32-P4
// ==========================================

// Version LVGL
#define LV_CONF_MAJOR 9
#define LV_CONF_MINOR 4
#define LV_CONF_PATCH 0

// ==========================================
// MÉMOIRE
// ==========================================
#define LV_MEM_CUSTOM 1
#define LV_MEM_SIZE (256 * 1024U)  // 256 KB heap LVGL

// ==========================================
// DISPLAY
// ==========================================
#define LV_COLOR_DEPTH 16  // RGB565 pour ESP32-P4
#define LV_DPI_DEF 100

// ==========================================
// THORVG VECTOR GRAPHICS (v9 only)
// ==========================================
#ifdef LV_USE_THORVG_INTERNAL
  #define LV_USE_THORVG_INTERNAL 1
#endif

#ifdef LV_USE_THORVG_EXTERNAL
  #define LV_USE_THORVG_EXTERNAL 1
#endif

// ==========================================
// FORMATS D'IMAGES
// ==========================================
#ifdef LV_USE_SVG
  #define LV_USE_SVG 1  // SVG via ThorVG
#endif

#ifdef LV_USE_LOTTIE
  #define LV_USE_LOTTIE 1  // Lottie animations via ThorVG
#endif

#ifdef LV_USE_GIF
  #define LV_USE_GIF 1
#endif

#ifdef LV_USE_BMP
  #define LV_USE_BMP 1
#endif

#ifdef LV_USE_LIBPNG
  #define LV_USE_LIBPNG 1
#endif

// ==========================================
// WIDGETS LVGL
// ==========================================
#define LV_USE_LABEL 1
#define LV_USE_IMAGE 1
#define LV_USE_BUTTON 1
#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_SLIDER 1
#define LV_USE_SPINNER 1
#define LV_USE_CANVAS 1

// ==========================================
// PERFORMANCE
// ==========================================
#define LV_USE_DRAW_SW_COMPLEX 1  // Rendu complexe optimisé
#define LV_IMG_CACHE_DEF_SIZE 8   // Cache 8 images
#define LV_SHADOW_CACHE_SIZE 16   // Cache ombres

// ==========================================
// LOGS
// ==========================================
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

#endif /*LV_CONF_H*/
```

### 1.3 - Créer `components/lvgl/lvgl_esphome.h`

**Rôle** : Interface C++ pour intégrer LVGL avec ESPHome

```cpp
#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "lvgl.h"

namespace esphome {
namespace lvgl {

class LvglComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

  // Initialiser LVGL
  void init_lvgl();

  // Vérifier version LVGL
  void check_version();

 protected:
  bool initialized_ = false;
};

}  // namespace lvgl
}  // namespace esphome
```

### 1.4 - Créer `components/lvgl/lvgl_esphome.cpp`

```cpp
#include "lvgl_esphome.h"
#include "esphome/core/log.h"

namespace esphome {
namespace lvgl {

static const char *const TAG = "lvgl";

void LvglComponent::setup() {
  ESP_LOGI(TAG, "Setting up LVGL v%d.%d.%d",
           LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

  this->init_lvgl();
  this->check_version();
}

void LvglComponent::init_lvgl() {
  lv_init();

  ESP_LOGI(TAG, "LVGL initialized successfully");

  // Afficher les features activés
#ifdef LV_USE_THORVG_INTERNAL
  ESP_LOGI(TAG, "ThorVG Internal: ENABLED");
#endif

#ifdef LV_USE_SVG
  ESP_LOGI(TAG, "SVG Support: ENABLED");
#endif

#ifdef LV_USE_LOTTIE
  ESP_LOGI(TAG, "Lottie Support: ENABLED");
#endif

  this->initialized_ = true;
}

void LvglComponent::check_version() {
  if (LVGL_VERSION_MAJOR < 9) {
    ESP_LOGE(TAG, "LVGL v9+ required but found v%d.%d.%d",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    ESP_LOGE(TAG, "ThorVG/SVG/Lottie requires LVGL v9+");
  } else {
    ESP_LOGI(TAG, "LVGL version check: OK (v9+ detected)");
  }
}

void LvglComponent::loop() {
  if (!this->initialized_) return;

  // Tâche LVGL (gestion événements, timers, etc.)
  lv_timer_handler();
}

}  // namespace lvgl
}  // namespace esphome
```

---

## 📦 Phase 2 : Adapter le Composant Storage

### 2.1 - Simplifier `components/storage/__init__.py`

**Changement** : Retirer la duplication des flags ThorVG (déjà dans `lvgl/`)

**Avant** (ligne 203-291) :
```python
# Configuration des décodeurs LVGL avancés intégré dans storage
if CONF_DECODERS in config:
    # ... beaucoup de code dupliqué ...
    if thorvg_cfg.get(CONF_THORVG_INTERNAL, False):
        cg.add_build_flag("-DLV_USE_THORVG_INTERNAL=1")
```

**Après** :
```python
# Les décodeurs LVGL sont maintenant configurés dans components/lvgl/
# On garde juste la configuration spécifique à storage ici
DEPENDENCIES = ["display", "lvgl"]  # ← Dépend du nouveau composant lvgl
```

### 2.2 - Garder la Détection de Formats

Le code dans `storage.cpp` pour détecter SVG/Lottie reste **inchangé** :

```cpp
// storage.cpp (ligne 1648-1686) - GARDER TEL QUEL
bool SdImageComponent::decode_svg_image(const std::vector<uint8_t> &svg_data) {
#ifdef LV_USE_SVG
    ESP_LOGD(TAG_IMAGE, "Using LVGL SVG decoder (LV_USE_SVG + ThorVG)");
    ESP_LOGE(TAG_IMAGE, "SVG decoding not yet implemented in storage component");
    return false;
#else
    ESP_LOGE(TAG_IMAGE, "SVG decoder not available");
    return false;
#endif
}
```

---

## 📦 Phase 3 : Configuration YAML Simplifiée

### 3.1 - Nouvelle Configuration

**Avant** (avec fork externe) :
```yaml
external_components:
  - source:
      type: git
      url: https://github.com/clydebarrow/esphome
      ref: lvgl-9.4
    components: [lvgl, font, image]
    refresh: 1d

lvgl_advanced_features:
  thorvg:
    internal: true
  svg: true
  lottie: true
```

**Après** (composant local) :
```yaml
# Plus besoin d'external_components pour LVGL !

lvgl:  # ← Nouveau composant local
  thorvg:
    internal: true
  svg: true
  lottie: true
  gif: true
  png: true

storage:
  # Configuration storage normale
  sd_images:
    - id: my_photo
      file_path: "/photos/photo.jpg"
```

---

## 📦 Phase 4 : Tests et Validation

### 4.1 - Compilation Test

```bash
# Nettoyer le cache
esphome clean votre_config.yaml

# Compiler avec le nouveau composant
esphome compile votre_config.yaml
```

### 4.2 - Vérifications dans les Logs

```
INFO ESPHome 2024.x.x
INFO Reading configuration votre_config.yaml...
INFO Detected timezone 'Europe/Paris'
INFO Adding library: lvgl/lvgl @ ^9.4.0  ✅
INFO Build flags: -DLV_USE_THORVG_INTERNAL=1  ✅
INFO Build flags: -DLV_USE_SVG=1  ✅
INFO Build flags: -DLV_USE_LOTTIE=1  ✅
```

### 4.3 - Tests Fonctionnels

1. **Test SVG** :
```yaml
lvgl:
  widgets:
    - image:
        src: "S:/icons/sun.svg"
        width: 64
        height: 64
```

2. **Test Lottie** :
```yaml
lvgl:
  widgets:
    - lottie:
        src: "S:/weather/clear-day.json"
        loop: true
        autoplay: true
```

---

## 📋 Checklist de Migration

- [ ] Phase 1 : Créer `components/lvgl/`
  - [ ] `__init__.py` avec lib_deps et flags
  - [ ] `lv_conf.h` avec ThorVG activé
  - [ ] `lvgl_esphome.h` interface C++
  - [ ] `lvgl_esphome.cpp` implémentation
  - [ ] `README.md` documentation

- [ ] Phase 2 : Adapter `components/storage/`
  - [ ] Retirer duplication flags ThorVG dans `__init__.py`
  - [ ] Ajouter dépendance `DEPENDENCIES = ["display", "lvgl"]`
  - [ ] Garder code détection SVG/Lottie dans `storage.cpp`

- [ ] Phase 3 : Fusionner/Supprimer `lvgl_advanced_features/`
  - [ ] Vérifier si encore nécessaire
  - [ ] Migrer fonctionnalités utiles vers `lvgl/`
  - [ ] Supprimer si redondant

- [ ] Phase 4 : Mettre à jour configurations YAML
  - [ ] Retirer `external_components` Git pour LVGL
  - [ ] Remplacer `lvgl_advanced_features:` par `lvgl:`
  - [ ] Tester compilation

- [ ] Phase 5 : Tests fonctionnels
  - [ ] Test SVG (icônes vectorielles)
  - [ ] Test Lottie (animations)
  - [ ] Test JPEG/GIF (storage)
  - [ ] Test caméra temps réel

- [ ] Phase 6 : Documentation
  - [ ] Mettre à jour `MIGRATION_LVGL_V9_README.md`
  - [ ] Créer `components/lvgl/README.md`
  - [ ] Exemples YAML à jour

---

## 🎯 Résultat Final

### Structure Finale

```
test2_esp_video_esphome/
├── components/
│   ├── lvgl/                   # ← NOUVEAU (remplace external_components)
│   │   ├── __init__.py        # Config LVGL v9.4 + ThorVG
│   │   ├── lv_conf.h          # Configuration LVGL
│   │   ├── lvgl_esphome.h
│   │   ├── lvgl_esphome.cpp
│   │   └── README.md
│   │
│   ├── storage/                # ← SIMPLIFIÉ (retire duplication ThorVG)
│   │   ├── __init__.py        # Dépend de lvgl/
│   │   ├── storage.h
│   │   └── storage.cpp
│   │
│   └── [autres composants...]
│
├── votre_config.yaml          # ← Simplifié (plus d'external_components)
└── MIGRATION_LVGL_V9_README.md  # ← Mis à jour
```

### Avantages

✅ **Indépendance** : Plus de dépendance externe Git
✅ **Simplicité** : Un seul composant `lvgl/` pour tout
✅ **Contrôle** : Maîtrise totale de LVGL + ThorVG
✅ **Maintenance** : Mise à jour via `lib_deps` PlatformIO
✅ **Partage** : Peut être publié comme external_component
✅ **Compatibilité** : Fonctionne avec ESPHome stable

---

## 🚀 Prochaines Étapes

1. **Créer les fichiers** de la Phase 1
2. **Tester la compilation** avec un fichier YAML minimal
3. **Adapter storage** (Phase 2)
4. **Migrer les configs** existantes (Phase 3)
5. **Tests fonctionnels** complets (Phase 4)

---

**Auteur** : Migration LVGL v9.4 Local
**Date** : 2026-01-13
**Version** : 1.0
