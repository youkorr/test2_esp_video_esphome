# ⚡ Démarrage Rapide - 5 Minutes

Guide ultra-rapide pour utiliser ce dépôt avec votre ESP32-P4.

---

## 📋 Prérequis

✅ ESP32-P4 avec PSRAM
✅ ESPHome 2024.x+
✅ Écran MIPI DSI ou RGB
✅ Carte SD (optionnel, pour SVG/Lottie)

---

## 🚀 Configuration Minimale

### 1. Créer `mon_appareil.yaml`

```yaml
esphome:
  name: mon-esp32-p4
  platform: esp32
  board: esp32-p4-function-ev-board

wifi:
  ssid: "VotreWiFi"
  password: "VotreMotDePasse"

# API Home Assistant
api:
  encryption:
    key: "votre_cle_api"

ota:
  password: "votre_mot_de_passe"

# ============================================
# COMPOSANTS EXTERNES (COPY-PASTE)
# ============================================
external_components:
  # Tous les composants depuis ce dépôt
  - source:
      type: git
      url: https://github.com/youkorr/test2_esp_video_esphome
    components:
      - lvgl     # LVGL v9.4 avec ThorVG/SVG/Lottie intégré
      - storage  # Support SD + images
      # Ajoutez d'autres composants si besoin

# ============================================
# CONFIGURATION ÉCRAN
# ============================================
display:
  - platform: ili9xxx  # Ou votre driver d'écran
    id: my_display
    model: ...
    # ... votre config écran ...

# ============================================
# LVGL + THORVG/SVG/LOTTIE
# ============================================
storage:
  decoders:
    thorvg:
      internal: true
    svg: true
    lottie: true

lvgl:
  displays:
    - my_display

  pages:
    - id: home
      widgets:
        - label:
            text: "Hello LVGL v9 + ThorVG!"
            x: 50
            y: 50
```

### 2. Compiler et Flasher

```bash
esphome run mon_appareil.yaml
```

### 3. Vérifier les Logs

Vous devriez voir :

```
[I][lvgl:XXX] LVGL version: 9.4.0 ✅
[I][storage:XXX] ThorVG Internal: ENABLED ✅
[I][storage:XXX] SVG Support: ENABLED ✅
[I][storage:XXX] Lottie Support: ENABLED ✅
```

---

## 🎨 Ajouter SVG/Lottie

### Préparer la Carte SD

```
/sdcard/
├── icons/
│   ├── sun.svg
│   ├── moon.svg
│   └── cloud.svg
└── animations/
    ├── loading.json (Lottie)
    └── weather.json (Lottie)
```

### Configuration

```yaml
# Carte SD
sd_mmc_card:
  id: sd_card
  clk_pin: GPIO13
  cmd_pin: GPIO11
  data_pins: [GPIO12]

# Storage avec SD
storage:
  sd_card_id: sd_card
  decoders:
    thorvg:
      internal: true
    svg: true
    lottie: true

# LVGL avec SVG et Lottie
lvgl:
  pages:
    - id: weather
      widgets:
        # Icône SVG (scalable)
        - image:
            src: "S:/icons/sun.svg"  # S: = carte SD
            width: 100
            height: 100
            x: 50
            y: 50

        # Animation Lottie (fluide 60 FPS)
        - lottie:
            src: "S:/animations/loading.json"
            width: 150
            height: 150
            x: 200
            y: 50
            loop: true
            autoplay: true
```

---

## 📹 Ajouter une Caméra

```yaml
external_components:
  # Ajouter ces composants
  - source:
      type: git
      url: https://github.com/youkorr/test2_esp_video_esphome
    components:
      - storage
      - esp_cam_sensor        # ← Nouveau
      - lvgl_camera_display   # ← Nouveau

# Configuration caméra
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

# Affichage caméra dans LVGL
lvgl_camera_display:
  camera_id: my_camera
  update_interval: 33ms  # 30 FPS
  lvgl_display: my_display
  x: 0
  y: 0
  width: 800
  height: 600
```

---

## 🔧 Troubleshooting Rapide

### Erreur : "Unknown widget type: lottie"

❌ **Cause** : LVGL v8 utilisé (v9 nécessaire)

✅ **Solution** : Vérifier `external_components` contient bien :
```yaml
- source:
    type: git
    url: https://github.com/youkorr/test2_esp_video_esphome
  components: [lvgl]  # ← LVGL v9.4 avec ThorVG
```

### Erreur : "ThorVG not enabled"

❌ **Cause** : ThorVG non activé dans storage

✅ **Solution** :
```yaml
storage:
  decoders:
    thorvg:
      internal: true  # ← Important !
    svg: true
    lottie: true
```

### Caméra ne démarre pas

❌ **Cause** : Pins incorrects ou résolution non supportée

✅ **Solution** : Vérifier les pins dans votre schéma matériel et essayer :
```yaml
esp_cam_sensor:
  resolution: 640x480  # Résolution plus faible pour test
```

### Out of Memory

❌ **Cause** : Pas assez de RAM

✅ **Solutions** :
1. Activer PSRAM :
```yaml
esphome:
  platformio_options:
    board_build.psram_type: "opi_opi"
```

2. Utiliser SVG au lieu de PNG (économise 90% RAM)

3. Réduire cache LVGL :
```yaml
storage:
  decoders:
    img_cache_size: 4  # Réduire de 8 à 4
```

---

## 📚 Prochaines Étapes

1. **Exemples complets** : Voir `README.md` section "Cas d'Usage"
2. **Migration LVGL v9** : Lire `MIGRATION_LVGL_V9_README.md`
3. **Optimisations** : Voir `OPTIMISATIONS_CAMERA_VIDEO.md`
4. **Composants** : Explorer `components/*/README.md`

---

## 💡 Templates Prêts à l'Emploi

### UI Smart Home
```bash
# Copy depuis le dépôt
cp exemples_lottie_svg_ui.yaml mon_config.yaml
```

### Dashboard Sécurité
```bash
# Copy depuis le dépôt
cp security_page_FINAL_WORKING.yaml mon_config.yaml
```

### Lecteur Vidéo
```bash
# Copy depuis le dépôt
cp avi_player_example.yaml mon_config.yaml
```

---

## 🆘 Support

- **Issues** : [GitHub Issues](https://github.com/youkorr/test2_esp_video_esphome/issues)
- **Discussions** : [GitHub Discussions](https://github.com/youkorr/test2_esp_video_esphome/discussions)
- **ESPHome Discord** : [discord.gg/esphome](https://discord.gg/esphome)

---

**Prêt à créer une UI moderne sur ESP32-P4 ? 🚀**
