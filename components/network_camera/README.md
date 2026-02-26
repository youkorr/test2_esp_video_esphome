# Network Camera Component for ESP32-P4

Component ESPHome pour afficher des flux vidéo réseau (RTSP/H264 et MJPEG) sur ESP32-P4 avec décodage matériel et affichage LVGL.

## 📋 Fonctionnalités

- ✅ **Support MJPEG** - Décodage matériel JPEG optimisé pour streams réseau
- ✅ **Support H264/RTSP** - Décodage logiciel H264 (Baseline/Main Profile)
- ✅ **Décodage matériel** - Hardware JPEG decoder ESP32-P4 (100ms timeout)
- ✅ **Suppression COM markers** - Compatibilité ffmpeg/go2rtc MJPEG
- ✅ **Gestion WiFi** - Attente automatique de la connexion WiFi (délai 15s)
- ✅ **Affichage LVGL** - Intégration native avec canvas LVGL
- ✅ **RGB565** - Format couleur optimisé pour affichage
- ✅ **Multi-résolution** - Support 320x240, 640x480, etc.

## 🔧 Prérequis

- **Hardware:** ESP32-P4 (avec décodeur JPEG matériel)
- **ESPHome:** Version récente avec support ESP32-P4
- **LVGL:** Component LVGL configuré
- **Réseau:** WiFi configuré et fonctionnel

## 📦 Installation

### 1. Ajouter le composant externe

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/youkorr/test2_esp_video_esphome
      ref: main
    components:
      - network_camera
      - multi_camera_display
    refresh: 0s
```

### 2. Configuration de base

```yaml
# Configuration réseau
wifi:
  ssid: "VotreSSID"
  password: "VotreMotDePasse"

# Configuration LVGL
lvgl:
  displays:
    - display_id: my_display

# Configuration caméra réseau
network_camera:
  - id: security_cam_1
    url: "http://:1984/api/stream.mjpeg?src=frigate1_esp32"
    protocol: mjpeg
    width: 320
    height: 240
    canvas_id: security_canvas
    update_interval: 100ms
```

## 🎬 Configuration MJPEG (Recommandé)

### Configuration go2rtc

Pour obtenir un flux MJPEG optimisé depuis vos caméras RTSP:

```yaml
# go2rtc.yaml (Frigate)
go2rtc:
  streams:
    frigate1_esp32:
      - "ffmpeg:rtsp://user:pass@/stream2#video=mjpeg#width=320#height=240#quality=80#fps=15"
```

### Configuration ESPHome

```yaml
network_camera:
  - id: security_cam_1
    url: "http:///api/stream.mjpeg?src=frigate1_esp32"
    protocol: mjpeg
    width: 320
    height: 240
    canvas_id: security_canvas
    update_interval: 100ms
```

**Pourquoi MJPEG?**
- ✅ Décodage matériel (rapide et efficace)
- ✅ Faible latence
- ✅ Pas de problèmes de profil H264
- ✅ COM markers automatiquement supprimés
- ✅ Validation JPEG intégrée

## 📺 Intégration LVGL

### Configuration complète avec boutons

```yaml
lvgl:
  pages:
    - id: security_page
      bg_color: 0x1a1a1a
      on_load:
        - lambda: |-
            ESP_LOGI("security", "Security page loaded - configuring canvas");

            // Configurer le canvas pour network_camera
            auto canvas = id(security_canvas);
            if (canvas != nullptr) {
              lv_coord_t w = lv_obj_get_width(canvas);
              lv_coord_t h = lv_obj_get_height(canvas);
              ESP_LOGI("security", "Canvas size: %dx%d", w, h);

              if (w > 0 && h > 0) {
                // IMPORTANT: Appeler configure_canvas sur security_cam_1
                id(security_cam_1).configure_canvas(canvas);
                ESP_LOGI("security", "✓ Canvas configured successfully!");
              } else {
                ESP_LOGW("security", "⚠ Canvas size is 0x0, waiting for initialization");
              }
            }

      widgets:
        - canvas:
            id: security_canvas
            width: 320
            height: 240
            x: 10
            y: 10
            bg_color: 0x000000

        - label:
            id: security_title
            text: "SECURITY CAMERA"
            x: 350
            y: 10
            text_color: 0xFFFFFF

        - button:
            id: btn_start_camera
            width: 100
            height: 40
            x: 350
            y: 60
            bg_color: 0x27ae60
            on_click:
              then:
                - lambda: |-
                    ESP_LOGI("security", "Starting camera");
                    id(security_cam_1).set_enabled(true);
            widgets:
              - label:
                  text: "START"
                  text_color: 0xFFFFFF
                  align: CENTER

        - button:
            id: btn_stop_camera
            width: 100
            height: 40
            x: 350
            y: 110
            bg_color: 0xe74c3c
            on_click:
              then:
                - lambda: |-
                    ESP_LOGI("security", "Stopping camera");
                    id(security_cam_1).set_enabled(false);
            widgets:
              - label:
                  text: "STOP"
                  text_color: 0xFFFFFF
                  align: CENTER

# Variables globales (optionnel)
globals:
  - id: cam1_state
    type: bool
    initial_value: 'false'
```

## 🔍 Résolution de problèmes

### Problème 1: "Canvas not configured"

**Symptôme:**
```
[W][network_camera]: Canvas not configured
```

**Solution:**
Appeler `configure_canvas()` dans le `on_load` de la page LVGL:

```yaml
on_load:
  - lambda: |-
      auto canvas = id(security_canvas);
      id(security_cam_1).configure_canvas(canvas);
```

**⚠️ IMPORTANT:** Appelez `configure_canvas()` sur `security_cam_1` (network_camera), PAS sur `security_display` (multi_camera_display)!

### Problème 2: "WiFi not ready yet"

**Symptôme:**
```
[W][network_camera]: ⏳ WiFi not ready yet, waiting for connection...
[E][network_camera]: Host is unreachable (errno 118)
```

**Solution:**
Le composant attend automatiquement la connexion WiFi avec un délai de 15 secondes entre les tentatives. **Aucune configuration requise**, c'est automatique!

Le composant vérifie:
1. WiFi est connecté
2. Interface STA est active
3. Avant toute tentative de connexion caméra

### Problème 3: "COM marker data underflow"

**Symptôme:**
```
[E][network_camera]: jpeg_parse_com_marker(63): COM marker data underflow
```

**Solution:**
✅ **Déjà corrigé!** Le composant supprime automatiquement les marqueurs COM ajoutés par ffmpeg/go2rtc qui sont incompatibles avec le décodeur ESP32-P4.

Logs attendus:
```
[I][network_camera]: Stripping COM marker at offset 2 (length 17 bytes)
[D][network_camera]: Stripped COM markers: 1585 → 1568 bytes (saved 17 bytes)
```

### Problème 4: H264 "No frames decoded"

**Symptôme:**
```
[W][network_camera]: No H264 frames decoded yet (1000 attempts)
```

**Solution:**
✅ **Déjà corrigé!** Le composant envoie maintenant SPS/PPS avec la **première frame** (I-frame ou P-frame), pas seulement avec les I-frames.

Logs attendus:
```
[I][network_camera]: ✓ Sent SPS+PPS (26+8 bytes) with FIRST frame (NAL type 1)
[I][network_camera]: ✓ First frame decoded successfully! Decoder initialized and working.
```

### Problème 5: Canvas taille 0x0

**Symptôme:**
```
[W][security]: Canvas size is 0x0, waiting for initialization
```

**Solution:**
Ne PAS configurer le canvas dans `on_load` de la page, mais dans un bouton après que LVGL soit initialisé:

```yaml
on_click:
  then:
    - lambda: |-
        static bool canvas_configured = false;
        if (!canvas_configured) {
          auto canvas = id(security_canvas);
          lv_coord_t w = lv_obj_get_width(canvas);
          if (w > 0 && lv_obj_get_height(canvas) > 0) {
            id(security_cam_1).configure_canvas(canvas);
            canvas_configured = true;
          }
        }
        id(security_cam_1).set_enabled(true);
```

## 📊 Logs de succès

### MJPEG fonctionnel

```
[I][network_camera]: ✓ WiFi ready, starting camera...
[I][network_camera]: MJPEG connected - Status: 200
[I][network_camera]: First JPEG frame: 1585 bytes
[I][network_camera]: Stripping COM marker at offset 2 (length 17 bytes)
[I][network_camera]: First JPEG frame analysis:
[I][network_camera]:   Size: 1568 bytes
[I][network_camera]:   SOI marker: 0xFFD8 (valid FFD8)
[I][network_camera]:   Format: Baseline DCT (SOF0) - fully supported ✓
[I][network_camera]: ✓ First JPEG decoded successfully: 153600 bytes output
[I][network_camera]: Frames: 100 - FPS: 15.0
```

### H264 fonctionnel

```
[I][network_camera]: ✓ WiFi ready, starting camera...
[I][network_camera]: RTSP connected
[I][network_camera]: SPS received: 26 bytes
[I][network_camera]: PPS received: 8 bytes
[I][network_camera]: ✓ Sent SPS+PPS (26+8 bytes) with FIRST frame (NAL type 1)
[I][network_camera]: Frame #1: NAL type 1 (P-frame), size 2847 bytes
[I][network_camera]: ✓ First frame decoded successfully! Decoder initialized and working.
[I][network_camera]:   Decoded YUV size: 115200 bytes
```

## ⚙️ Configuration H264/RTSP

### Configuration caméra Tapo

```yaml
network_camera:
  - id: security_cam_1
    url: "rtsp://username:password@192.168.1.56:554/stream2"
    protocol: h264
    width: 320
    height: 240
    canvas_id: security_canvas
    update_interval: 100ms
```

### Configuration go2rtc (proxy H264)

```yaml
go2rtc:
  streams:
    frigate1:
      - rtsp://username:password@192.168.1.56:554/stream1
```

**⚠️ Limitations H264:**
- Supporte uniquement **Baseline** et **Main Profile**
- **High Profile** (Tapo C500 par défaut) non supporté
- GOP important peut causer des délais
- Plus lent que MJPEG (décodage logiciel)

**💡 Recommandation:** Utilisez MJPEG via go2rtc pour de meilleures performances!

## 🎯 Configuration multi-caméras

```yaml
network_camera:
  - id: security_cam_1
    url: "http://192.168.1.38:1984/api/stream.mjpeg?src=cam1"
    protocol: mjpeg
    width: 320
    height: 240
    canvas_id: canvas1

  - id: security_cam_2
    url: "http://192.168.1.38:1984/api/stream.mjpeg?src=cam2"
    protocol: mjpeg
    width: 320
    height: 240
    canvas_id: canvas2

multi_camera_display:
  id: security_display
  canvas_id: main_canvas
  cameras:
    - camera_id: security_cam_1
    - camera_id: security_cam_2
```

## 📝 API Lambda

### Méthodes disponibles

```cpp
// Activer/désactiver la caméra
id(security_cam_1).set_enabled(true);
id(security_cam_1).set_enabled(false);

// Configurer le canvas LVGL
auto canvas = id(security_canvas);
id(security_cam_1).configure_canvas(canvas);

// Vérifier l'état
bool is_running = id(security_cam_1).is_enabled();
```

## 🔬 Détails techniques

### Correctifs appliqués

1. **Fix H264 SPS/PPS critique**
   - Envoie SPS/PPS avec la PREMIÈRE frame (pas seulement I-frames)
   - Évite le "No frames decoded" quand stream commence avec P-frames
   - Fichier: `network_camera.cpp:924-981`

2. **Fix MJPEG COM markers**
   - Suppression des marqueurs COM (FF FE) ajoutés par ffmpeg
   - ESP32-P4 hardware decoder ne supporte pas COM markers
   - Fonction: `strip_jpeg_com_markers_()`
   - Fichier: `network_camera.cpp:423-472`

3. **Fix JPEG timeout**
   - Timeout augmenté de 40ms → 100ms
   - Nécessaire pour latence réseau
   - Fichier: `network_camera.cpp:248-262`

4. **Fix WiFi timing**
   - Attente automatique connexion WiFi
   - Délai 15s entre tentatives
   - Vérifie `is_connected()` et `has_sta()`
   - Fichier: `network_camera.cpp:54-96`

5. **Fix WiFi API compatibility**
   - Remplace `get_ip_address()` par `has_sta()`
   - Compatible avec nouvelles versions ESPHome
   - Fichier: `network_camera.cpp:74-82`

### Format de données

- **MJPEG Input:** JPEG Baseline DCT (SOF0)
- **H264 Input:** NAL units Annex B format (00 00 00 01)
- **Output:** RGB565 (2 bytes/pixel)
- **Buffer:** 320x240 = 153600 bytes RGB565

### Performance

- **MJPEG:** ~15 FPS @ 320x240 (décodage matériel)
- **H264:** ~10 FPS @ 320x240 (décodage logiciel)
- **Mémoire SRAM:** ~220 KB
- **Mémoire PSRAM:** ~6.7 MB

## 🐛 Debugging

### Activer les logs détaillés

```yaml
logger:
  level: DEBUG
  logs:
    network_camera: DEBUG
```

### Vérifier le stream

```bash
# Test MJPEG dans navigateur
http://192.168.1.38:1984/api/stream.mjpeg?src=frigate1_esp32

# Test H264 avec ffplay
ffplay -rtsp_transport tcp rtsp://user:pass@192.168.1.56:554/stream2
```

## 📚 Ressources

- **Repository:** https://github.com/youkorr/test2_esp_video_esphome
- **Branch:** `claude/fix-mjpeg-streaming-seuCV`
- **ESPHome:** https://esphome.io
- **go2rtc:** https://github.com/AlexxIT/go2rtc

## 📄 Licence

Voir LICENSE dans le repository principal.

## 🙏 Support

Pour des problèmes ou questions:
1. Vérifier la section "Résolution de problèmes" ci-dessus
2. Activer les logs DEBUG
3. Créer une issue sur GitHub avec les logs complets

---

**Version:** 1.0.0 (2025-12-30)
**Branche:** claude/fix-mjpeg-streaming-seuCV
