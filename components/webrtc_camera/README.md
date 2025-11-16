# WebRTC Camera Component for ESPHome

## 📋 Description

Ce composant ESPHome permet le streaming vidéo **H.264** en temps réel depuis un ESP32-P4 avec caméra MIPI-CSI via **WebRTC simplifié**. Il utilise l'encodeur matériel H.264 de l'ESP32-P4 pour un streaming à faible latence et haute qualité.

## ✨ Caractéristiques

- ✅ **Encodage H.264 matériel** via l'accélérateur ESP32-P4
- ✅ **Streaming RTP** avec protocole temps réel
- ✅ **Signalisation WebSocket** pour la négociation SDP
- ✅ **Client Web intégré** - Ouvrez simplement un navigateur
- ✅ **Faible latence** (~100-200ms)
- ✅ **Bande passante optimisée** (1-3 Mbps pour 720p@30fps)
- ✅ **Support multi-résolution** (VGA, 720p, 1080p)
- ✅ **Configuration flexible** du bitrate et de la qualité

## 🔧 Matériel requis

- **ESP32-P4** (Function EV Board ou équivalent)
- **Caméra MIPI-CSI** (OV5647, OV02C10, SC202CS, etc.)
- **PSRAM** (recommandé pour les buffers vidéo)
- **Réseau WiFi** (connexion LAN pour de meilleures performances)

## 📦 Installation

1. Copiez le dossier `webrtc_camera` dans votre répertoire `components/` ESPHome

2. Ajoutez la configuration dans votre fichier YAML :

```yaml
# Configuration I2C pour la caméra
i2c:
  - id: bsp_bus
    sda: GPIO14
    scl: GPIO13
    frequency: 400kHz

# Caméra MIPI-CSI
mipi_dsi_cam:
  id: main_camera
  i2c_id: bsp_bus
  sensor_type: sc202cs
  resolution: 720P
  pixel_format: RGB565
  framerate: 30

# Composant WebRTC
webrtc_camera:
  camera_id: main_camera
  signaling_port: 8443
  rtp_port: 5004
  bitrate: 2000000
  gop: 30
  qp_min: 10
  qp_max: 40
```

3. Compilez et flashez sur votre ESP32-P4

## 🚀 Utilisation

### 1. Démarrage

Après le démarrage de l'ESP32-P4, vous verrez dans les logs :

```
[webrtc_camera] WebRTC Camera setup complete
[webrtc_camera] Signaling server: http://<IP>:8443
[webrtc_camera] RTP port: 5004
```

### 2. Connexion avec un navigateur

1. Ouvrez votre navigateur (Chrome, Firefox, Safari)
2. Allez sur `http://<ESP32_IP>:8443`
3. Cliquez sur **"Start Stream"**
4. La vidéo H.264 s'affichera en temps réel

### 3. Connexion avec VLC/FFmpeg

Vous pouvez également utiliser des outils comme VLC ou FFmpeg :

```bash
# Avec FFmpeg
ffmpeg -protocol_whitelist file,udp,rtp -i sdp.txt -f sdl "ESP32 Camera"

# Avec VLC
vlc rtp://192.168.1.100:5004
```

## ⚙️ Options de configuration

| Paramètre | Type | Défaut | Description |
|-----------|------|--------|-------------|
| `camera_id` | ID | **requis** | ID de la caméra `mipi_dsi_cam` |
| `signaling_port` | int | `8443` | Port WebSocket pour signalisation SDP |
| `rtp_port` | int | `5004` | Port UDP pour le streaming RTP |
| `bitrate` | int | `2000000` | Bitrate cible en bps (2 Mbps) |
| `gop` | int | `30` | Group of Pictures (période I-frame) |
| `qp_min` | int | `10` | QP minimum (0-51, plus bas = meilleure qualité) |
| `qp_max` | int | `40` | QP maximum (0-51, plus haut = plus de compression) |

### Recommandations par résolution

| Résolution | Bitrate | GOP | QP Min | QP Max |
|------------|---------|-----|--------|--------|
| 640x480 (VGA) | 1 Mbps | 25 | 15 | 35 |
| 1280x720 (720p) | 2 Mbps | 30 | 10 | 40 |
| 1920x1080 (1080p) | 4 Mbps | 30 | 8 | 45 |

## 🏗️ Architecture technique

```
┌─────────────────────────────────────────────────────┐
│                    ESP32-P4                          │
├─────────────────────────────────────────────────────┤
│                                                      │
│  Camera (MIPI-CSI)                                  │
│         ↓                                            │
│  ISP Pipeline (Bayer → RGB565)                      │
│         ↓                                            │
│  RGB565 → YUV420 Conversion                         │
│         ↓                                            │
│  H.264 Hardware Encoder                             │
│         ├─→ SPS/PPS/IDR/P frames                    │
│         ↓                                            │
│  NAL Unit Parser                                    │
│         ↓                                            │
│  RTP Packetizer                                     │
│         ↓                                            │
│  UDP Socket (RTP)                                   │
│                                                      │
│  WebSocket (Signaling)                              │
│         ↓                                            │
└─────────┼──────────────────────────────────────────┘
          │
          │ RTP/UDP Stream (H.264)
          ↓
   ┌──────────────┐
   │   Browser    │
   │  (WebRTC)    │
   └──────────────┘
```

## 🔍 API H.264 utilisée

Le composant utilise l'API ESP-IDF H.264 :

```cpp
// Encodeur matériel
esp_h264_enc_cfg_hw_t cfg = {
    .pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY,  // YUV420
    .gop = 30,
    .fps = 30,
    .res = {.width = 1280, .height = 720},
    .rc = {.bitrate = 2000000, .qp_min = 10, .qp_max = 40}
};

esp_h264_enc_hw_new(&cfg, &encoder);
esp_h264_enc_open(encoder);
esp_h264_enc_process(encoder, &in_frame, &out_frame);
```

## 📊 Format RTP H.264

Le composant implémente :

- **RFC 3550** : RTP (Real-time Transport Protocol)
- **RFC 6184** : RTP Payload Format for H.264 Video
- **Payload Type** : 96 (dynamique)
- **Clock Rate** : 90000 Hz (standard pour vidéo)

Format des paquets RTP :

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|V=2|P|X|  CC   |M|     PT      |       Sequence Number         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           Timestamp                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                             SSRC                              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         H.264 NAL Unit                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

## 🐛 Débogage

### Activer les logs détaillés

```yaml
logger:
  level: DEBUG
  logs:
    webrtc_camera: VERBOSE
```

### Problèmes courants

#### 1. Pas de vidéo dans le navigateur

- Vérifiez que le port RTP (5004) n'est pas bloqué par le pare-feu
- Assurez-vous que l'ESP32 et le client sont sur le même réseau LAN
- Vérifiez les logs pour les erreurs d'encodage H.264

#### 2. Latence élevée

- Réduisez le GOP (ex: 15 au lieu de 30)
- Augmentez le bitrate
- Vérifiez la qualité du réseau WiFi

#### 3. Mauvaise qualité d'image

- Augmentez le bitrate (ex: 3000000 pour 3 Mbps)
- Réduisez le QP max (ex: 35 au lieu de 40)
- Augmentez la résolution de la caméra

#### 4. Erreur "Failed to create H.264 encoder"

- Vérifiez que votre ESP32-P4 a suffisamment de PSRAM
- Réduisez la résolution ou le bitrate
- Vérifiez les logs pour plus de détails

## 📝 Limitations actuelles

1. **Pas de support ICE/STUN/TURN complet** : Connexion directe LAN uniquement
2. **Pas de fragmentation FU-A** : Les NAL units > 1400 octets ne sont pas fragmentés
3. **Pas de SRTP** : Streaming non chiffré (OK pour LAN)
4. **Un seul client** : Un seul client WebRTC à la fois

## 🛣️ Roadmap

- [ ] Support fragmentation FU-A pour grandes NAL units
- [ ] Support multi-clients simultanés
- [ ] SRTP pour streaming sécurisé
- [ ] Support ICE basique (STUN)
- [ ] Intégration Home Assistant native
- [ ] Support audio (AAC/Opus)

## 🧪 Tests

Pour tester l'encodeur H.264 sans WebRTC :

```yaml
# Utilisez le composant camera_web_server avec format JPEG
# puis comparez avec webrtc_camera en H.264

# Test 1: MJPEG (baseline)
camera_web_server:
  camera_id: main_camera
  enable_stream: true

# Test 2: WebRTC H.264
webrtc_camera:
  camera_id: main_camera
```

## 📚 Références

- [ESP-IDF H.264 Component](https://github.com/espressif/esp-h264)
- [RFC 3550 - RTP Protocol](https://tools.ietf.org/html/rfc3550)
- [RFC 6184 - RTP Payload Format for H.264](https://tools.ietf.org/html/rfc6184)
- [WebRTC Specification](https://www.w3.org/TR/webrtc/)

## 📄 Licence

Ce composant utilise le code ESP-IDF sous licence Apache 2.0.

## 👤 Auteur

- [@youkorr](https://github.com/youkorr)

## 🤝 Contributions

Les contributions sont les bienvenues ! N'hésitez pas à ouvrir une issue ou une pull request.

---

**Note** : Ce composant est conçu spécifiquement pour tester l'encodeur/décodeur H.264 sur ESP32-P4. Pour une solution WebRTC complète en production, des améliorations supplémentaires seraient nécessaires (ICE, STUN, TURN, etc.).
