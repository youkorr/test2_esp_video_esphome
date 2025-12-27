# Guide d'utilisation du lecteur AVI pour ESPHome

## Vue d'ensemble

Le composant `avi_player` permet de lire des fichiers vidéo AVI (format MJPEG + PCM audio) sur des dispositifs ESP32 avec ESPHome et LVGL.

## Fonctionnalités supportées

- ✅ Lecture de fichiers AVI depuis le système de fichiers (SPIFFS, SD Card)
- ✅ Format vidéo MJPEG
- ✅ Format audio PCM
- ✅ Lecture en boucle
- ✅ Contrôle via actions ESPHome (play, stop)
- ✅ Intégration LVGL pour l'affichage

## Prérequis

1. **Matériel requis:**
   - ESP32-S3 ou ESP32-P4 (recommandé pour de meilleures performances)
   - Écran compatible LVGL
   - Carte SD ou partition SPIFFS pour stocker les fichiers AVI

2. **Dépendances ESPHome:**
   - `lvgl` - Interface graphique
   - `esp-idf` framework version >= 4.4

## Installation

### 1. Copier les fichiers

Assurez-vous que le dossier `components/avi_player` contient tous les fichiers nécessaires:

```
components/avi_player/
├── __init__.py                  # Intégration ESPHome
├── avi_player_component.h       # Header C++
├── avi_player_component.cpp     # Implémentation C++
├── avi_player.h                 # API C originale
├── avi_player.c                 # Implémentation C
├── avifile.h
├── avifile.c
├── avi_def.h
└── CMakeLists.txt
```

### 2. Préparer votre fichier AVI

Le fichier AVI doit être au format MJPEG avec audio PCM. Utilisez FFmpeg pour convertir vos vidéos:

```bash
ffmpeg -i input.mp4 \
  -vcodec mjpeg -q:v 5 \
  -acodec pcm_s16le -ar 48000 -ac 2 \
  -s 480x270 \
  output.avi
```

**Paramètres recommandés:**
- Résolution: 480x270 ou 800x480 (selon votre écran)
- Video codec: MJPEG
- Audio codec: PCM 16-bit
- Sample rate: 48000 Hz
- Channels: 2 (stéréo) ou 1 (mono)

### 3. Télécharger le fichier AVI sur l'ESP32

#### Option A: Utiliser SPIFFS

1. Créer une partition SPIFFS dans `partitions.csv`:
```csv
# Name,     Type, SubType, Offset,   Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
factory,  0,    0,       ,        0x90000,
avi,      data, spiffs,  ,        0x300000,
```

2. Monter SPIFFS dans votre code ESPHome (voir exemple ci-dessous)

3. Télécharger le fichier via OTA ou USB

#### Option B: Utiliser une carte SD

Configurez le composant SD card dans ESPHome et placez votre fichier AVI sur la carte.

## Configuration ESPHome

### Configuration minimale

```yaml
# Configuration ESPHome de base
esphome:
  name: avi-player
  friendly_name: AVI Player

esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: esp-idf
    version: 5.4.0

logger:

# LVGL est requis
lvgl:
  displays:
    - display_id: my_display

# Configuration de l'affichage (à adapter à votre matériel)
display:
  - platform: ili9xxx
    id: my_display
    model: ili9341
    # ... configuration de vos pins ...

# Lecteur AVI
avi_player:
  - id: my_video
    file_path: "/spiffs/video.avi"
    width: 480
    height: 270
    auto_play: true
    loop: true
```

### Configuration avancée

```yaml
avi_player:
  - id: my_video
    file_path: "/sdcard/MJPEG/video.avi"  # Chemin du fichier AVI
    width: 800                            # Largeur de la vidéo
    height: 480                           # Hauteur de la vidéo
    buffer_size: 122880                   # Taille du buffer (120KB)
    auto_play: false                      # Lecture automatique au démarrage
    loop: true                            # Lecture en boucle
    show_controls: true                   # Afficher contrôles play/stop
    show_slider: true                     # Afficher slider de progression
    preload_to_memory: false              # Précharger en PSRAM (SD lente)
    fps: 25                               # Override FPS
    parent_id: my_lvgl_container          # Parent LVGL (optionnel)
```

### Paramètres de configuration

| Paramètre | Type | Défaut | Description |
|-----------|------|--------|-------------|
| `file_path` | string | **requis** | Chemin du fichier AVI |
| `width` | int | 480 | Largeur de la vidéo en pixels |
| `height` | int | 270 | Hauteur de la vidéo en pixels |
| `buffer_size` | int | 61440 (60KB) | Taille du buffer interne |
| `auto_play` | bool | true | Démarrer la lecture automatiquement |
| `loop` | bool | false | Lecture en boucle |
| `show_controls` | bool | false | Afficher les boutons play/stop |
| `show_slider` | bool | false | Afficher le slider de progression |
| `preload_to_memory` | bool | false | Précharger le fichier en PSRAM |
| `fps` | float | auto | Override FPS (0-120) |
| `parent_id` | id | lv_scr_act() | Objet LVGL parent |

## Actions disponibles

### avi_player.play

Lance la lecture de la vidéo.

```yaml
button:
  - platform: template
    name: "Play Video"
    on_press:
      - avi_player.play: my_video
```

### avi_player.stop

Arrête la lecture de la vidéo.

```yaml
button:
  - platform: template
    name: "Stop Video"
    on_press:
      - avi_player.stop: my_video
```

## Exemples d'utilisation

### Exemple 1: Lecture automatique au démarrage

```yaml
avi_player:
  - id: intro_video
    file_path: "/spiffs/intro.avi"
    auto_play: true
    loop: false
```

### Exemple 2: Contrôle via boutons

```yaml
avi_player:
  - id: demo_video
    file_path: "/sd/demo.avi"
    auto_play: false

binary_sensor:
  - platform: gpio
    pin: GPIO0
    name: "Play Button"
    on_press:
      - avi_player.play: demo_video

  - platform: gpio
    pin: GPIO1
    name: "Stop Button"
    on_press:
      - avi_player.stop: demo_video
```

### Exemple 3: Contrôle depuis Home Assistant

```yaml
avi_player:
  - id: ha_video
    file_path: "/spiffs/video.avi"
    auto_play: false

api:
  services:
    - service: play_video
      then:
        - avi_player.play: ha_video

    - service: stop_video
      then:
        - avi_player.stop: ha_video
```

Depuis Home Assistant, vous pouvez ensuite appeler:
```yaml
service: esphome.avi_player_play_video
```

## Fichier de test

Un fichier AVI de test a été téléchargé dans:
```
components/avi_player/test_apps/spiffs/p4_introduce.avi
```

Ce fichier peut être utilisé pour tester le composant:
- Résolution: 480x270
- Format: MJPEG
- Audio: PCM 48kHz stéréo
- Taille: ~2.4 MB

## Dépannage

### Erreur "Failed to allocate video buffer"

**Solution:** Augmentez la taille du PSRAM ou réduisez la résolution de la vidéo.

```yaml
esphome:
  platformio_options:
    board_build.arduino.memory_type: qio_opi
```

### Erreur "Failed to play file"

**Vérifications:**
1. Le chemin du fichier est correct
2. Le fichier existe sur le système de fichiers
3. Le format du fichier est MJPEG AVI
4. La partition SPIFFS est correctement montée

### Vidéo saccadée

**Solutions:**
1. Réduire la résolution de la vidéo
2. Augmenter `buffer_size`
3. Réduire la qualité JPEG (paramètre `-q:v` de FFmpeg)
4. Utiliser un ESP32-P4 pour de meilleures performances

### Pas de son

L'audio n'est pas encore implémenté dans cette version. Pour ajouter le support audio, vous devrez:
1. Ajouter un composant `speaker` ESPHome
2. Implémenter `audio_frame_callback` dans `avi_player_component.cpp`

## Différences avec simple_video_player

| Fonctionnalité | avi_player | simple_video_player |
|----------------|------------|---------------------|
| Format vidéo | MJPEG AVI | MJPEG, H.264, MP4 |
| Format audio | PCM | AAC, PCM |
| Complexité | Simple | Avancé |
| Taille du code | Petit | Grand |
| Performances | Bon | Meilleur |

## Roadmap

- [ ] Support audio complet
- [ ] Décodage JPEG matériel (ESP32-P4)
- [ ] Support des fichiers en mémoire
- [ ] Pause/Resume
- [ ] Contrôle de la vitesse de lecture
- [ ] Événements (on_start, on_end, on_error)

## Licence

Apache License 2.0 - Voir le fichier LICENSE pour plus de détails.

## Crédits

Basé sur le composant AVI Player d'Espressif Systems:
https://github.com/espressif/esp-iot-solution/tree/master/components/avi_player
