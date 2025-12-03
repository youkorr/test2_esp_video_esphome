# Diagnostic : Lecteur vidéo ne s'affiche pas

## Problème
Le composant `simple_video_player` ne s'affiche pas à l'écran.

## Checklist de vérification

### 1. Configuration YAML de base

Vérifiez que vous avez **toutes** ces lignes :

```yaml
simple_video_player:
  id: my_video_player
  file_path: "http://192.168.1.158:8080/video.mp4"  # ou chemin local
  parent_id: video_page  # ← IMPORTANT : doit exister !
  show_controls: true
  auto_play: false
```

### 2. Le `parent_id` existe-t-il ?

**ERREUR FRÉQUENTE** : Le `parent_id` doit pointer vers un objet LVGL existant.

#### Option A : Utiliser l'écran principal

Si vous n'avez pas de pages définies, utilisez l'écran par défaut :

```yaml
simple_video_player:
  id: my_video_player
  file_path: "/data/video.mp4"
  # PAS de parent_id si vous utilisez l'écran principal !
  show_controls: true
```

#### Option B : Créer une page dédiée

```yaml
lvgl:
  displays:
    - display_id: my_display

  pages:
    - id: video_page  # ← Créer la page d'abord !
      widgets:
        - obj:
            width: 800
            height: 480

simple_video_player:
  id: my_video_player
  file_path: "/data/video.mp4"
  parent_id: video_page  # ← Maintenant ça fonctionne
```

### 3. Vérifier les logs de démarrage

Cherchez ces logs dans la sortie ESPHome :

#### ✅ Logs OK (composant fonctionne)

```
[C][simple_video_player:257] Simple Video Player:
[C][simple_video_player:258]   File: /data/video.mp4 (ou http://...)
[C][simple_video_player:261]   Format: MP4/H.264
[I][simple_video_player:...] Video file opened: 5242880 bytes
[I][simple_video_player:...] H.264 decoder initialized for 640x480
[I][yuv_rgb:37] YUV→RGB conversion initialized (BT.601 colorspace)
```

#### ❌ Logs d'erreur

```
[E][simple_video_player:...] Failed to open file: /data/video.mp4
→ Le fichier n'existe pas ou le chemin est incorrect

[E][simple_video_player:...] parent_id not found
→ Le parent_id n'existe pas dans LVGL

[E][simple_video_player:...] Failed to create canvas
→ Problème LVGL (mémoire insuffisante ?)

[E][simple_video_player:...] Failed to initialize H.264 decoder
→ Problème avec le décodeur H.264
```

### 4. Configuration minimale pour tester

Essayez cette configuration **ultra-simple** pour tester :

```yaml
esphome:
  name: test-video

esp32:
  board: esp32-s3-devkitc-1
  variant: esp32s3
  framework:
    type: esp-idf

psram:
  mode: octal
  speed: 80MHz

# Display basique
spi:
  clk_pin: GPIO12
  mosi_pin: GPIO11

display:
  - platform: ili9xxx
    model: ili9341
    id: my_display
    cs_pin: GPIO10
    dc_pin: GPIO9
    dimensions:
      width: 320
      height: 240

# LVGL
lvgl:
  displays:
    - display_id: my_display

# Lecteur vidéo SANS parent_id
simple_video_player:
  id: my_video_player
  file_path: "http://192.168.1.158:8080/test.mp4"
  width: 320
  height: 240
  show_controls: true
  auto_play: true  # ← Lance automatiquement
```

### 5. Test avec un fichier local d'abord

Avant de tester HTTP, assurez-vous que le lecteur fonctionne avec un fichier local :

```yaml
simple_video_player:
  id: my_video_player
  file_path: "/data/test.mp4"  # ← Fichier sur carte SD
  show_controls: true
  auto_play: true
```

**Si ça ne fonctionne pas** → Problème avec le lecteur lui-même, pas HTTP.
**Si ça fonctionne** → Problème spécifique à HTTP.

### 6. Activer les logs DEBUG

Pour voir exactement ce qui se passe :

```yaml
logger:
  level: DEBUG
  logs:
    simple_video_player: VERBOSE
    lvgl: DEBUG
```

Puis cherchez dans les logs :
- Erreurs de création d'UI
- Problèmes d'allocation mémoire
- Erreurs LVGL

### 7. Vérifier la mémoire disponible

Le lecteur nécessite beaucoup de mémoire SPIRAM. Ajoutez des logs au démarrage :

```yaml
esphome:
  on_boot:
    - lambda: |-
        ESP_LOGI("memory", "Free heap: %d bytes", esp_get_free_heap_size());
        ESP_LOGI("memory", "Free SPIRAM: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
```

**Attendu** : Au moins 2-3 MB de SPIRAM libre.

### 8. Problèmes fréquents et solutions

#### Problème : "Failed to create canvas"
**Cause** : Pas assez de mémoire pour le buffer RGB.
**Solution** :
- Réduire la résolution (`width: 320, height: 240`)
- Vérifier que SPIRAM est configuré

#### Problème : Écran noir mais pas d'erreur
**Cause** : Lecteur créé mais pas visible (problème de parent ou de z-order).
**Solution** :
```yaml
simple_video_player:
  # ... autres paramètres ...
  # Retirer parent_id pour utiliser l'écran principal
```

#### Problème : HTTP download bloque tout
**Cause** : Download trop long, timeout.
**Solution** : Tester avec un fichier TRÈS petit d'abord (< 1 MB).

### 9. Configuration complète testée

Voici une config **garantie de fonctionner** (adaptez à votre matériel) :

```yaml
esphome:
  name: esp32-video-test
  platformio_options:
    board_build.flash_mode: dio
    board_build.psram_type: opi

esp32:
  board: esp32s3-devkitc-1
  variant: esp32s3
  framework:
    type: esp-idf
    version: recommended

psram:
  mode: octal
  speed: 80MHz

wifi:
  ssid: "VOTRE_SSID"
  password: "VOTRE_PASSWORD"

logger:
  level: DEBUG

# Display (adaptez à votre écran)
spi:
  clk_pin: GPIO12
  mosi_pin: GPIO11

display:
  - platform: ili9xxx
    model: ili9341
    id: main_display
    cs_pin: GPIO10
    dc_pin: GPIO9
    dimensions:
      width: 320
      height: 240

lvgl:
  displays:
    - display_id: main_display
  log_level: INFO

# LECTEUR VIDÉO - Configuration minimale
simple_video_player:
  id: video
  file_path: "http://192.168.1.100:8080/small_test.mp4"
  width: 320
  height: 240
  show_controls: true
  auto_play: true
  loop: true
```

### 10. Étapes de diagnostic

1. **Compiler et flasher**
2. **Ouvrir les logs** (ESPHome logs ou serial monitor)
3. **Chercher** `[C][simple_video_player:` dans les logs
4. **Noter** toutes les erreurs
5. **M'envoyer** les logs pour diagnostic précis

---

## Besoin d'aide ?

Envoyez-moi :
1. ✅ Votre configuration YAML complète
2. ✅ Les logs de démarrage (première minute)
3. ✅ Confirmation : ESP32-S3 ou ESP32-P4 ?
4. ✅ Taille de SPIRAM (8MB, 16MB, 32MB ?)

Et je pourrai vous aider précisément ! 🔧
