# 🎥 ESP32-P4 Video & LVGL Components for ESPHome

[![ESPHome](https://img.shields.io/badge/ESPHome-2024.x-blue.svg)](https://esphome.io)
[![LVGL](https://img.shields.io/badge/LVGL-v9.4-green.svg)](https://lvgl.io)
[![ESP32](https://img.shields.io/badge/ESP32-P4-orange.svg)](https://www.espressif.com/en/products/socs/esp32-p4)

Collection de composants ESPHome optimisés pour ESP32-P4, avec support **LVGL v9.4 + ThorVG** (SVG/Lottie), caméra temps réel, lecture vidéo, et stockage SD avancé.

---

## ✨ Fonctionnalités

### 🎨 Graphics & UI (LVGL v9.4 + ThorVG)
- ✅ **SVG Support** : Icônes vectorielles scalables (économie de 90% RAM)
- ✅ **Lottie Animations** : Animations vectorielles fluides 60 FPS
- ✅ **ThorVG Engine** : Rendu vectoriel accéléré GPU/PPA
- ✅ **Multi-formats** : JPEG, GIF, PNG, BMP depuis carte SD

### 📹 Video & Camera
- ✅ **Caméra temps réel** : 30 FPS stable avec affichage LVGL
- ✅ **Lecteur vidéo** : Support AVI/H.264 avec sync A/V < 30ms
- ✅ **Multi-caméras** : Support RTSP/Frigate pour caméras réseau
- ✅ **Détection IA** : YOLO11, détection visage, piétons

### 💾 Storage & Media
- ✅ **Carte SD** : Lecture images/vidéos haute performance
- ✅ **Formats avancés** : JPEG, GIF, SVG, Lottie, PNG, BMP
- ✅ **Cache intelligent** : Optimisation RAM avec PPA accelerator

### ⚡ Performance ESP32-P4
- ✅ **PPA Accelerator** : Accélération matérielle pour images
- ✅ **PSRAM** : Gestion optimisée mémoire externe
- ✅ **DMA** : Transferts DMA pour affichage MIPI DSI

---

## 🚀 Démarrage Rapide

### Prérequis

- **Matériel** : ESP32-P4 avec PSRAM (ex: ESP32-P4-Function-EV-Board)
- **ESPHome** : Version 2024.x ou supérieure
- **Carte SD** : Pour images/vidéos (optionnel)
- **Écran** : MIPI DSI ou RGB (ex: 800x480, 1024x600, 1280x720)

### Installation

#### 1. Configuration ESPHome

Créez votre fichier `.yaml` avec cette configuration :

```yaml
# Nom de votre appareil
esphome:
  name: esp32-p4-display
  platform: esp32
  board: esp32-p4-function-ev-board

# Composants externes (tout depuis ce dépôt)
external_components:
  - source:
      type: git
      url: https://github.com/youkorr/test2_esp_video_esphome
    components:
      - lvgl                 # LVGL v9.4 avec ThorVG/SVG/Lottie intégré
      - storage              # Support SD + images avancées
      - esp_cam_sensor       # Caméra optimisée ESP32-P4
      - lvgl_camera_display  # Affichage caméra dans LVGL
      # Ajoutez d'autres selon vos besoins

# C'est tout ! ThorVG/SVG/Lottie sont activés automatiquement
```

#### 2. Compilation et Flash

```bash
# Compiler le firmware
esphome compile votre_config.yaml

# Flasher sur ESP32-P4
esphome upload votre_config.yaml

# Voir les logs
esphome logs votre_config.yaml
```

#### 3. Vérifier l'Installation

Dans les logs, vous devriez voir :

```
[I][lvgl:XXX] LVGL version: 9.4.0 ✅
[I][storage:XXX] ThorVG Internal: ENABLED ✅
[I][storage:XXX] SVG Support: ENABLED ✅
[I][storage:XXX] Lottie Support: ENABLED ✅
```

---

## 📦 Composants Disponibles

### Core Components

| Composant | Description | Cas d'usage |
|-----------|-------------|-------------|
| **storage** | Gestion SD + ThorVG/SVG/Lottie | Chargement images/vidéos depuis SD |
| **esp_cam_sensor** | Driver caméra optimisé | Caméra temps réel ESP32-P4 |
| **lvgl_camera_display** | Widget LVGL caméra | Afficher flux caméra dans UI |
| **ppa_accelerator** | Accélération PPA | Rotations/conversions images matérielles |

### Advanced Components

| Composant | Description | Cas d'usage |
|-----------|-------------|-------------|
| **avi_player** | Lecteur vidéo AVI | Lecture vidéos locales |
| **simple_video_player** | Lecteur vidéo simple | Vidéos basiques sans son |
| **video_player** | Lecteur vidéo avancé | Vidéos avec sync A/V |
| **network_camera** | Caméra réseau RTSP | Afficher caméras IP/Frigate |
| **multi_camera_display** | Affichage multi-caméras | Dashboard sécurité |

### AI & Detection Components

| Composant | Description | Cas d'usage |
|-----------|-------------|-------------|
| **yolo11_detection** | Détection objets YOLO11 | Reconnaissance objets en temps réel |
| **face_detection** | Détection visage | Déverrouillage par visage |
| **human_face_recognition** | Reconnaissance faciale | Identification personnes |
| **pedestrian_detection** | Détection piétons | Surveillance extérieure |

---

## 📚 Exemples de Configuration

### Exemple 1 : UI avec SVG et Lottie

```yaml
lvgl:
  displays:
    - platform: ...  # Votre écran

  pages:
    - id: weather_page
      widgets:
        # Icône météo SVG (vectorielle, scalable)
        - image:
            id: weather_icon
            src: "S:/weather/icons/sun.svg"  # S: = carte SD
            width: 128
            height: 128
            x: 50
            y: 50

        # Animation Lottie (fluide 60 FPS)
        - lottie:
            id: weather_animation
            src: "S:/weather/animations/clear-day.json"
            width: 200
            height: 200
            x: 300
            y: 50
            loop: true
            autoplay: true

        # Label texte
        - label:
            text: "25°C - Ensoleillé"
            x: 50
            y: 200
```

**Ressources Lottie gratuites** :
- [Weather Icons by Basmilius](https://github.com/basmilius/weather-icons)
- [LottieFiles Free](https://lottiefiles.com/free)

### Exemple 2 : Caméra Temps Réel

```yaml
# Configuration caméra ESP32-P4
esp_cam_sensor:
  id: my_camera
  model: OV5647  # ou OV02C10, SC202CS
  data_pins: [4, 5, 6, 7, 15, 16, 17, 18]
  vsync_pin: 19
  href_pin: 20
  pclk_pin: 21
  reset_pin: 22
  resolution: 800x600
  jpeg_quality: 12

# Affichage dans LVGL
lvgl_camera_display:
  camera_id: my_camera
  update_interval: 33ms  # 30 FPS
  lvgl_display: my_display
  x: 0
  y: 0
  width: 800
  height: 600
```

### Exemple 3 : Images depuis Carte SD

```yaml
# Configuration carte SD
sd_mmc_card:
  id: sd_card
  clk_pin: GPIO13
  cmd_pin: GPIO11
  data_pins: [GPIO12]

# Composant storage (avec ThorVG)
storage:
  sd_card_id: sd_card

  decoders:
    thorvg:
      internal: true
    svg: true
    lottie: true

  sd_images:
    - id: photo1
      file_path: "/photos/family.jpg"

    - id: logo
      file_path: "/icons/company_logo.svg"  # SVG vectoriel

# Affichage dans LVGL
lvgl:
  widgets:
    - image:
        src: photo1  # Utilise storage pour JPEG

    - image:
        src: "S:/icons/company_logo.svg"  # Utilise ThorVG pour SVG
```

---

## 🎯 Cas d'Usage Courants

### 🏠 Dashboard Smart Home

Interface tactile pour domotique avec :
- Icônes SVG pour pièces/appareils (scalables, peu de RAM)
- Animations Lottie pour statuts (ex: lumière qui s'allume)
- Caméras surveillance en direct (RTSP/Frigate)
- Contrôles tactiles LVGL

**👉 Voir** : `examples/smart_home_dashboard.yaml` (à venir)

### 🌦️ Station Météo

Affichage météo moderne avec :
- Icônes météo SVG (jour/nuit/pluie/nuages...)
- Animations Lottie météo fluides
- Graphiques températures LVGL
- Prévisions avec API

**👉 Voir** : `exemples_lottie_svg_ui.yaml`

### 🔐 Système de Sécurité

Dashboard multi-caméras avec :
- Affichage 4-9 caméras simultanées
- Détection mouvement/personnes (YOLO11)
- Enregistrement vidéo sur SD
- Reconnaissance faciale

**👉 Voir** : `security_page_FINAL_WORKING.yaml`

### 📹 Lecteur Multimédia

Lecteur vidéo/photos avec :
- Lecture vidéos AVI/H.264 depuis SD
- Galerie photos JPEG/PNG
- Contrôles lecture (play/pause/volume)
- Interface tactile LVGL

**👉 Voir** : `avi_player_example.yaml`

---

## 📖 Documentation Détaillée

### Guides de Migration

- **[MIGRATION_LVGL_V9_README.md](MIGRATION_LVGL_V9_README.md)** : Migration complète LVGL v8 → v9 avec ThorVG
- **[GUIDE_MIGRATION_V9_RAPIDE.md](GUIDE_MIGRATION_V9_RAPIDE.md)** : Migration rapide en 4 étapes
- **[PLAN_INTEGRATION_LVGL_V9.md](PLAN_INTEGRATION_LVGL_V9.md)** : Plan d'intégration LVGL v9.4 local

### Composants Spécifiques

- **[components/storage/README.md](components/storage/README.md)** : Documentation complète du composant storage
- **[components/lvgl_advanced_features/README.md](components/lvgl_advanced_features/README.md)** : Features avancées LVGL (ThorVG)

### Optimisations

- **[OPTIMISATIONS_CAMERA_VIDEO.md](OPTIMISATIONS_CAMERA_VIDEO.md)** : Optimisations caméra et vidéo pour ESP32-P4

---

## 🔧 Configuration Avancée

### ThorVG Configuration

```yaml
storage:
  decoders:
    thorvg:
      internal: true   # ThorVG intégré dans LVGL (recommandé)
      external: false  # ThorVG externe (nécessite librairie)

    svg: true          # Support SVG
    lottie: true       # Support Lottie
    gif: true          # Support GIF
    png: true          # Support PNG via libpng
    bmp: true          # Support BMP

    # Options performance
    draw_sw_complex: true     # Rendu complexe optimisé
    img_cache_size: 8         # Cache 8 images
    shadow_cache_size: 16     # Cache 16 ombres
```

### PPA Accelerator

```yaml
# Accélération matérielle pour rotations/conversions
ppa_accelerator:
  id: ppa
  enable_rotation: true
  enable_scale: true
  enable_color_conversion: true
```

### Multi-Caméras Réseau

```yaml
# Caméra Frigate
network_camera:
  - id: cam_entree
    name: "Caméra Entrée"
    frigate_url: "http://192.168.1.100:5000"
    camera_name: "front_door"
    stream_type: "main"
    update_interval: 100ms

# Affichage multi-caméras
multi_camera_display:
  cameras:
    - cam_entree
    - cam_jardin
    - cam_garage
  layout: grid_2x2
  lvgl_display: my_display
```

---

## 🐛 Troubleshooting

### Erreur : "Unknown widget type: lottie"

**Cause** : LVGL v8 ne supporte pas Lottie (nécessite v9)

**Solution** :
```yaml
# Ajouter external_components pour LVGL v9.4
external_components:
  - source:
      type: git
      url: https://github.com/clydebarrow/esphome
      ref: lvgl-9.4
    components: [lvgl, font, image]
```

### Erreur : "SVG decoder not available"

**Cause** : ThorVG non activé dans storage

**Solution** :
```yaml
storage:
  decoders:
    thorvg:
      internal: true  # ← Activer ThorVG
    svg: true         # ← Activer SVG
```

### Caméra : FPS faible (< 20 FPS)

**Causes possibles** :
1. Résolution trop élevée
2. PPA non activé
3. Timer trop lent

**Solutions** :
```yaml
# 1. Réduire résolution
esp_cam_sensor:
  resolution: 640x480  # Au lieu de 1280x720

# 2. Activer PPA
ppa_accelerator:
  id: ppa
  enable_rotation: true

# 3. Timer plus rapide
lvgl_camera_display:
  update_interval: 33ms  # 30 FPS
```

### Mémoire insuffisante (Out of Memory)

**Solutions** :
1. Utiliser SVG au lieu de PNG/JPEG pour icônes (-90% RAM)
2. Réduire cache LVGL
3. Activer PSRAM

```yaml
# Configuration PSRAM
esphome:
  platformio_options:
    board_build.psram_type: "opi_opi"
```

---

## 🤝 Contribution

Les contributions sont les bienvenues !

### Comment Contribuer

1. **Fork** ce dépôt
2. **Créer une branche** : `git checkout -b feature/ma-feature`
3. **Commiter** : `git commit -m "Add: Ma fonctionnalité"`
4. **Push** : `git push origin feature/ma-feature`
5. **Pull Request** sur ce dépôt

### Code Style

- **Python** : PEP 8
- **C++** : Style ESPHome (clang-format)
- **YAML** : 2 espaces d'indentation
- **Commentaires** : En français ou anglais

---

## 📄 Licence

Ce projet utilise différentes licences selon les composants :

- **Composants originaux** : Apache 2.0
- **LVGL** : MIT License
- **ThorVG** : MIT License
- **ESP-IDF** : Apache 2.0

Voir fichiers LICENSE individuels dans chaque composant.

---

## 🙏 Remerciements

- **ESPHome Team** : Pour le framework incroyable
- **LVGL Team** : Pour la bibliothèque UI
- **ThorVG Team** : Pour le moteur vectoriel
- **Espressif** : Pour les ESP32-P4 et ESP-IDF
- **Clyde Barrow** : Pour le fork LVGL v9.4 ESPHome
- **Communauté Home Assistant** : Pour le support

---

## 📞 Support

- **Issues** : [GitHub Issues](https://github.com/youkorr/test2_esp_video_esphome/issues)
- **Discussions** : [GitHub Discussions](https://github.com/youkorr/test2_esp_video_esphome/discussions)
- **ESPHome Discord** : [discord.gg/esphome](https://discord.gg/esphome)

---

## 🗺️ Roadmap

### Court Terme
- [ ] Templates de configuration par cas d'usage
- [ ] Exemples vidéos tutoriels
- [ ] Tests automatisés
- [ ] Documentation API complète

### Moyen Terme
- [ ] Support LVGL v9 ESPHome officiel (quand disponible)
- [ ] Composant wrapper `lvgl_thorvg` standalone
- [ ] Support ESP32-P4 RISC-V cores
- [ ] Optimisations GPU supplémentaires

### Long Terme
- [ ] Support ESP32-S3 (limité)
- [ ] Builder UI en ligne pour configurations
- [ ] Marketplace templates communautaires
- [ ] Integration avec ESPHome Add-on Home Assistant

---

**⭐ Si ce projet vous est utile, pensez à mettre une étoile sur GitHub !**

Made with ❤️ for the ESPHome community
