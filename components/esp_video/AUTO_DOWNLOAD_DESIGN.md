# ESP-Video Auto-Download Design - Comme LVGL 9.4

## 🎯 Objectif

Faire en sorte que **esp_video télécharge et installe automatiquement** toutes ses dépendances pendant la compilation, exactement comme LVGL 9.4.

## 📦 Comment LVGL 9.4 fonctionne

```python
async def to_code(config):
    # LVGL télécharge automatiquement depuis le registry PlatformIO/Arduino
    cg.add_library("lvgl/lvgl", "9.4.0")      # Auto-download LVGL
    cg.add_library("pngdec", "1.0.1")          # Auto-download PNG decoder

    # Configuration...
```

**Résultat** : Pendant la compilation, ESPHome télécharge automatiquement :
- `lvgl/lvgl` version 9.4.0 depuis le registry
- `pngdec` version 1.0.1 depuis le registry

## 🔧 Solutions pour ESP-Video

### Option 1: ESP-IDF Component Manager (RECOMMANDÉ)

**Avantages** :
- ✅ Utilise le système officiel ESP-IDF
- ✅ Composants vérifiés et maintenus par Espressif
- ✅ Gestion automatique des versions
- ✅ Cache local pour builds rapides

**Composants ESP-IDF disponibles** :
- `espressif/esp_h264` - Encodeur/décodeur H.264
- `espressif/esp_new_jpeg` - Encodeur/décodeur JPEG
- `espressif/esp_codec_dev` - Gestion devices codec
- Potentiellement : `esp_cam_sensor`, `esp_ipa`, `esp_sccb_intf`

**Implémentation** :

```python
# components/esp_video/__init__.py

async def to_code(config):
    # Vérification ESP-IDF
    if not CORE.using_esp_idf:
        raise cv.Invalid("ESP-Video nécessite ESP-IDF")

    # ============================================================
    # TÉLÉCHARGEMENT AUTOMATIQUE DES DÉPENDANCES (comme LVGL)
    # ============================================================

    # Méthode 1: Via cg.add_idf_component() si disponible
    if hasattr(cg, 'add_idf_component'):
        cg.add_idf_component("espressif/esp_h264", "1.3.1")
        cg.add_idf_component("espressif/esp_new_jpeg", "1.0.0")
        cg.add_idf_component("espressif/esp_codec_dev", "1.2.0")

    # Méthode 2: Via idf_component.yml automatique
    else:
        # Créer idf_component.yml à la volée
        create_idf_component_manifest(config)

    # Reste de la configuration...
```

### Option 2: Git Clone Automatique

**Avantages** :
- ✅ Contrôle total sur les sources
- ✅ Peut utiliser des versions/branches spécifiques
- ✅ Fonctionne même si composants pas sur registry

**Implémentation** :

```python
import subprocess
import os

async def to_code(config):
    component_dir = os.path.dirname(__file__)
    parent_dir = os.path.dirname(component_dir)

    # ============================================================
    # AUTO-DOWNLOAD DEPENDENCIES (comme LVGL)
    # ============================================================

    dependencies = [
        {
            "name": "esp_h264",
            "url": "https://github.com/espressif/esp-adf-libs.git",
            "version": "v1.3.1",
            "path": "esp_h264"
        },
        {
            "name": "esp_cam_sensor",
            "url": "https://github.com/espressif/esp-adf-libs.git",
            "version": "v1.3.1",
            "path": "esp_cam_sensor"
        },
    ]

    for dep in dependencies:
        dep_dir = os.path.join(parent_dir, dep["name"])

        if not os.path.exists(dep_dir):
            logging.info(f"📥 Téléchargement {dep['name']} v{dep['version']}...")

            # Clone sparse (seulement le composant nécessaire)
            subprocess.run([
                "git", "clone", "--depth=1", "--branch", dep["version"],
                "--filter=blob:none", "--sparse",
                dep["url"], dep_dir
            ], check=True)

            subprocess.run([
                "git", "-C", dep_dir, "sparse-checkout", "set", dep["path"]
            ], check=True)

            logging.info(f"✅ {dep['name']} installé !")

    # Configuration normale...
```

### Option 3: PlatformIO Library Manager

**Avantages** :
- ✅ Utilise le même système que LVGL
- ✅ Simple à implémenter

**Limitation** :
- ❌ esp_video n'est probablement pas sur le registry PlatformIO

**Implémentation** :

```python
async def to_code(config):
    # Si disponible sur PlatformIO Registry
    cg.add_library("espressif/esp_h264", "1.3.1")
    cg.add_library("espressif/esp_cam_sensor", "1.0.0")

    # Configuration...
```

## 🎬 Implémentation Recommandée

**Approche hybride** : ESP-IDF Component Manager + fallback Git Clone

```python
async def to_code(config):
    if not CORE.using_esp_idf:
        raise cv.Invalid("ESP-Video nécessite ESP-IDF")

    # ============================================================
    # AUTO-DOWNLOAD (comme LVGL 9.4)
    # ============================================================

    logging.info("📦 ESP-Video: Téléchargement automatique des dépendances...")

    # Essayer d'abord ESP-IDF Component Manager
    try:
        ensure_esp_idf_components(config)
        logging.info("✅ Dépendances téléchargées via ESP-IDF Component Manager")
    except Exception as e:
        # Fallback: Git clone
        logging.warning(f"⚠️ Component Manager échoué: {e}")
        logging.info("📥 Téléchargement via Git clone...")
        ensure_components_via_git(config)
        logging.info("✅ Dépendances téléchargées via Git")

    # Configuration normale (includes, flags, etc.)
    configure_esp_video(config)
```

## 📋 Liste des Dépendances à Télécharger

Pour ESP-Video complet, il faut :

1. **esp_h264** - Encodeur/décodeur H.264
   - Source: `espressif/esp_h264` ou GitHub esp-adf-libs
   - Version: 1.3.1+

2. **esp_cam_sensor** - Drivers caméra (OV5647, SC202CS, OV02C10)
   - Source: esp-adf-libs
   - Inclut les configs JSON IPA

3. **esp_ipa** - Image Processing Algorithms
   - Source: esp-adf-libs
   - Pour AWB, denoise, sharpen, gamma, CC

4. **esp_sccb_intf** - Interface I2C/SCCB pour caméras
   - Source: esp-adf-libs

5. **Bibliothèques statiques**
   - `libtinyh264.a` - Décodeur H.264 Baseline
   - `libopenh264.a` - Encodeur H.264
   - `libesp_ipa.a` - IPA interne

## 🚀 Workflow Utilisateur

**Avant (état actuel)** :
```yaml
# L'utilisateur doit manuellement :
# 1. Cloner esp-adf-libs
# 2. Copier esp_h264, esp_cam_sensor, etc.
# 3. Configurer les chemins
# 4. Espérer que tout compile

esp_video:
  i2c_id: i2c_bus
  enable_h264: true
```

**Après (avec auto-download)** :
```yaml
# L'utilisateur configure simplement :
# ESPHome télécharge TOUT automatiquement pendant la compilation !

esp_video:
  i2c_id: i2c_bus
  enable_h264: true
  enable_jpeg: true

# BOOM ! Ça compile automatiquement avec toutes les dépendances !
```

## 📝 Prochaines Étapes

1. ✅ Créer `ensure_esp_idf_components()` function
2. ✅ Créer `ensure_components_via_git()` fallback
3. ✅ Modifier `__init__.py` pour intégrer l'auto-download
4. ✅ Tester la compilation avec dépôt vide
5. ✅ Documenter le nouveau workflow

---

**Résultat final** : ESP-Video fonctionne EXACTEMENT comme LVGL 9.4 - téléchargement et installation automatiques ! 🎉
