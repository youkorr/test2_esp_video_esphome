# Camera Web Server Component for ESPHome (ESP32-P4)

Serveur web HTTP pour caméra MIPI-DSI sur ESP32-P4, compatible ESPHome.

## État Actuel

⚠️ **VERSION ALPHA - JPEG ENCODING À IMPLÉMENTER**

**Fonctionnel:**
- ✅ Serveur HTTP sur port configurable
- ✅ Endpoints: `/pic`, `/stream`, `/status`
- ✅ Interface avec `mipi_dsi_cam` component
- ✅ Structure MJPEG stream multipart

**En cours de développement:**
- ⚠️ **Encodage JPEG hardware** - Actuellement envoie RGB565 brut (incompatible navigateur)
- ⚠️ Support H.264 streaming
- ⚠️ Authentification HTTP
- ⚠️ Optimisation performance

## Configuration YAML

```yaml
# Caméra MIPI-DSI
mipi_dsi_cam:
  id: my_camera
  sensor: sc202cs
  resolution: "720P"
  pixel_format: "RGB565"
  framerate: 30

# Serveur Web Caméra
camera_web_server:
  camera_id: my_camera
  port: 8080
  enable_stream: true
  enable_snapshot: true
```

## Utilisation

Une fois déployé, accédez aux endpoints:

- **Snapshot**: `http://<ip-address>:8080/pic`
- **Stream MJPEG**: `http://<ip-address>:8080/stream`
- **Status JSON**: `http://<ip-address>:8080/status`

## Endpoints API

### GET /pic
Capture une image JPEG unique.

**Headers:**
- `Content-Type: image/jpeg`
- `Access-Control-Allow-Origin: *`

**Exemple:**
```bash
curl http://192.168.1.100:8080/pic > snapshot.jpg
```

### GET /stream
Stream MJPEG continu (multipart/x-mixed-replace).

**Headers:**
- `Content-Type: multipart/x-mixed-replace;boundary=...`
- `X-Framerate: 30`

**Utilisation HTML:**
```html
<img src="http://192.168.1.100:8080/stream" />
```

### GET /status
Retourne le statut de la caméra en JSON.

**Réponse:**
```json
{
  "streaming": true,
  "width": 1280,
  "height": 720,
  "format": "RGB565"
}
```

## Prochaines Étapes (TODO)

### 1. Encodage JPEG Hardware (PRIORITÉ 1)

Intégrer le JPEG encoder hardware ESP32-P4:

```cpp
#include "driver/jpeg_encode.h"

// Dans setup():
jpeg_encode_engine_cfg_t encode_cfg = {
    .timeout_ms = 5000,
};
jpeg_new_encoder_engine(&encode_cfg, &jpeg_handle_);

// Dans snapshot_handler_():
jpeg_encode_cfg_t config = {
    .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
    .sub_sample = JPEG_DOWN_SAMPLING_YUV422,
    .image_quality = 80,
    .width = camera_->get_image_width(),
    .height = camera_->get_image_height(),
};

uint32_t output_size;
jpeg_encoder_process(jpeg_handle_, &config,
                     rgb565_buffer, rgb565_size,
                     jpeg_buffer, jpeg_buffer_size,
                     &output_size);
```

**Référence:** `/components/esp_video/exemples/simple_video_server/main/simple_video_server_example.c`

### 2. Support Format Natif JPEG

Si `pixel_format: "JPEG"` configuré dans `mipi_dsi_cam`, utiliser directement sans réencodage:

```cpp
if (camera_->get_pixel_format() == "JPEG") {
    // Envoyer directement, pas de réencodage
    httpd_resp_send(req, (const char *)image_data, image_size);
} else {
    // Encoder RGB565 → JPEG
    jpeg_encoder_process(...);
}
```

### 3. Optimisations Performance

- Buffer pool pour JPEG output (réutiliser au lieu de réallouer)
- Zero-copy quand format source est déjà JPEG
- Compression adaptative selon débit réseau

### 4. Sécurité

- Authentification HTTP Basic
- HTTPS/TLS optionnel
- Rate limiting par client

### 5. Fonctionnalités Avancées

- Paramètres d'URL: `?quality=80`, `?size=640x480`
- WebSocket streaming pour faible latence
- Support H.264/RTSP
- Enregistrement vidéo sur SD card

## Architecture

```
┌─────────────────┐
│  mipi_dsi_cam   │ ← Capture frames (RGB565/JPEG/YUV)
└────────┬────────┘
         │
         v
┌─────────────────┐
│ camera_web_     │ ← HTTP Server (esp_http_server)
│ server          │
└────────┬────────┘
         │
         v
┌─────────────────┐
│  JPEG Encoder   │ ← Hardware (si RGB565 source)
│  (ESP32-P4)     │
└────────┬────────┘
         │
         v
    HTTP Client
   (Navigateur)
```

## Dépendances

- `mipi_dsi_cam` - Composant caméra MIPI-DSI
- ESP-IDF `esp_http_server` component
- ESP-IDF `driver/jpeg_encode.h` (à ajouter)

## Limitations Actuelles

1. **Pas d'encodage JPEG** - Envoie RGB565 brut (incompatible)
2. **Framerate limité** - Delay fixe 33ms (~30 FPS max)
3. **Pas d'authentification** - Accès public
4. **Format unique** - Seulement RGB565 supporté

## Références

- [ESP-Video Simple Video Server Example](../esp_video/exemples/simple_video_server/)
- [ESP-IDF HTTP Server](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_server.html)
- [ESP32-P4 JPEG Encoder](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/jpeg.html)

## Contribution

Pour implémenter l'encodage JPEG:

1. Ajouter `#include "driver/jpeg_encode.h"` dans `camera_web_server.cpp`
2. Créer `jpeg_encoder_handle_t` dans `setup()`
3. Allouer buffer JPEG output
4. Modifier `snapshot_handler_()` et `stream_handler_()` pour encoder

Voir l'exemple complet dans `/components/esp_video/exemples/simple_video_server/`.
