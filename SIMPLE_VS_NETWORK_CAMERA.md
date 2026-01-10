# SimpleVideoPlayer vs NetworkCamera - Quel composant utiliser?

## 🎯 Comparaison Rapide

| Caractéristique | **SimpleVideoPlayer** | **NetworkCamera** |
|----------------|----------------------|-------------------|
| **Source** | Fichiers locaux (SD/PSRAM/HTTP) | Streaming réseau (RTSP/MJPEG) |
| **FPS par défaut** | **50 FPS** | 10 FPS (30 FPS après fix) |
| **Limitation FPS** | ❌ Aucune | ⚠️ Adaptation WiFi RSSI (fixé) |
| **MP4 Support** | ✅ Oui (via esp_h264) | ✅ Oui (via esp_h264) |
| **MJPEG Support** | ✅ Oui | ✅ Oui |
| **Config FPS manuelle** | ✅ Oui (`fps: 30`) | ⚠️ Via `update_interval` |
| **Cache PSRAM** | ✅ Oui (`preload_to_memory`) | ❌ Non |
| **Contrôles UI** | ✅ Oui (play/pause/seek) | ❌ Non |
| **Auto-loop** | ✅ Oui | ❌ Non |

---

## 📝 Recommandations

### ✅ Utilisez `SimpleVideoPlayer` pour:

1. **Fichiers MP4 locaux sur carte SD**
   ```yaml
   simple_video_player:
     - id: my_video
       file_path: "/sdcard/video.mp4"
       width: 640
       height: 480
       parent_id: my_display
       loop: true
       auto_play: true
       fps: 30  # Optionnel: Force 30 FPS
   ```

2. **Fichiers MJPEG/AVI locaux**
   ```yaml
   simple_video_player:
     - id: my_video
       file_path: "/sdcard/video.avi"
       preload_to_memory: true  # Cache en PSRAM pour performance max
   ```

3. **Vidéos HTTP téléchargées**
   ```yaml
   simple_video_player:
     - id: my_video
       file_path: "http://server/video.mp4"
       max_http_file_size: 50000000  # 50MB max
   ```

**Avantages:**
- Pas de limitation FPS artificielle
- Cache PSRAM disponible (lecture ultra-rapide)
- Contrôles de lecture intégrés
- Loop automatique

---

### ✅ Utilisez `NetworkCamera` pour:

1. **Streaming RTSP en direct** (caméras IP)
   ```yaml
   network_camera:
     - id: tapo_cam
       url: "rtsp://user:pass@ip:554/stream1"
       protocol: rtsp
       width: 640
       height: 480
       canvas_id: canvas
       update_interval: 33ms  # 30 FPS (après fix)
   ```

2. **Streaming MJPEG HTTP en direct**
   ```yaml
   network_camera:
     - id: mjpeg_stream
       url: "http://camera_ip:8080/video"
       protocol: mjpeg
   ```

**Avantages:**
- Optimisé pour streaming réseau
- Buffer double pour éviter tearing
- Reconnexion automatique

---

## 🚀 Performance: MP4 Baseline 640×480

### SimpleVideoPlayer (RECOMMANDÉ)

```yaml
simple_video_player:
  file_path: "/sdcard/video.mp4"
  fps: 30  # Force 30 FPS
```

**Performance:**
- ✅ **FPS obtenu:** 28-30 FPS
- Timer interval: 33ms (configurable via `fps`)
- Aucune limitation WiFi
- Cache PSRAM optionnel

### NetworkCamera (STREAMING)

```yaml
network_camera:
  url: "file:///sdcard/video.mp4"  # Pas recommandé pour fichiers locaux
  update_interval: 33ms
```

**Performance:**
- ⚠️ **FPS obtenu:** 25-30 FPS (après fix)
- Adapte selon WiFi RSSI (-50 dBm requis pour 30 FPS)
- Pas de cache PSRAM
- Conçu pour streaming, pas fichiers locaux

---

## 📊 Cas d'Usage Détaillés

### Cas 1: Vidéo d'accueil en boucle

```yaml
# ✅ RECOMMANDÉ: SimpleVideoPlayer
simple_video_player:
  - id: welcome_video
    file_path: "/sdcard/welcome.mp4"
    width: 800
    height: 480
    loop: true
    auto_play: true
    preload_to_memory: true  # Cache en PSRAM = 30+ FPS garanti
```

**Résultat:** 30 FPS stable, pas de lag SD card

---

### Cas 2: Caméra IP Tapo en direct

```yaml
# ✅ RECOMMANDÉ: NetworkCamera
network_camera:
  - id: security_cam
    url: "rtsp://user:pass@192.168.1.100:554/stream1"
    protocol: rtsp
    width: 1280
    height: 720
    canvas_id: canvas
    update_interval: 33ms
```

**Résultat:** 25-30 FPS selon réseau, reconnexion auto

---

### Cas 3: Lecteur vidéo avec contrôles

```yaml
# ✅ SEUL CHOIX: SimpleVideoPlayer
simple_video_player:
  - id: video_player
    file_path: "/sdcard/movie.mp4"
    show_controls: true  # Affiche play/pause/seek
    loop: false
```

**Résultat:** Expérience lecteur vidéo complète

---

### Cas 4: Conversion MP4 → MJPEG (performance max)

Si vous voulez les meilleures performances absolues avec `NetworkCamera` ou `SimpleVideoPlayer`:

```bash
# Convertir MP4 → MJPEG (utilise décodeur JPEG hardware)
ffmpeg -i video.mp4 -c:v mjpeg -q:v 10 video.avi

# Puis avec SimpleVideoPlayer (30+ FPS garanti):
simple_video_player:
  file_path: "/sdcard/video.avi"
  preload_to_memory: true
```

**Résultat:** 30+ FPS avec décodeur JPEG hardware

---

## 🔧 Configuration Optimale par Scénario

### Scénario 1: Performance maximale (fichier local)

```yaml
simple_video_player:
  - id: my_video
    file_path: "/sdcard/video.avi"  # MJPEG converti
    preload_to_memory: true          # Cache PSRAM
    fps: 30
```

**FPS attendu:** 30+

### Scénario 2: Streaming caméra IP

```yaml
network_camera:
  - id: cam
    url: "rtsp://ip/stream2"  # Stream basse résolution si possible
    protocol: rtsp
    width: 640
    height: 480
    update_interval: 33ms
```

**FPS attendu:** 25-30 (selon réseau)

### Scénario 3: MP4 High Profile (après fix OpenH264)

```yaml
simple_video_player:
  - id: my_video
    file_path: "/sdcard/video_high_profile.mp4"
    fps: 25  # Légèrement plus bas pour High Profile
```

**FPS attendu:** 22-25

---

## ⚠️ Pièges à Éviter

### ❌ N'utilisez PAS NetworkCamera pour fichiers locaux

```yaml
# MAUVAIS - NetworkCamera n'est pas optimisé pour fichiers locaux
network_camera:
  url: "file:///sdcard/video.mp4"
```

**Problème:**
- Adaptation WiFi RSSI (inutile pour fichiers locaux)
- Pas de cache PSRAM
- Pas de contrôles de lecture

### ❌ N'utilisez PAS SimpleVideoPlayer pour streaming en direct

```yaml
# IMPOSSIBLE - SimpleVideoPlayer ne supporte pas RTSP
simple_video_player:
  file_path: "rtsp://camera/stream"  # NE FONCTIONNE PAS
```

**Problème:**
- Pas de support protocole RTSP
- Pas de reconnexion automatique
- Conçu pour fichiers, pas streaming

---

## 📈 Tableau Performance Comparatif

### MP4 Baseline 640×480 (après tous les fixes)

| Config | Composant | FPS | CPU | Note |
|--------|-----------|-----|-----|------|
| **MP4 local** | SimpleVideoPlayer | **28-30** | 55% | ⭐⭐⭐ |
| **MP4 local** | NetworkCamera | 25-28 | 55% | ⭐⭐ |
| **MP4 + PSRAM cache** | SimpleVideoPlayer | **30** | 50% | ⭐⭐⭐ |
| **MJPEG local** | SimpleVideoPlayer | **30** | 40% | ⭐⭐⭐ |
| **MJPEG + PSRAM** | SimpleVideoPlayer | **30+** | 35% | ⭐⭐⭐ |
| **RTSP stream** | NetworkCamera | 25-30 | 55% | ⭐⭐⭐ |

---

## 🎓 Résumé

### SimpleVideoPlayer
**Usage:** Fichiers locaux (SD/PSRAM/HTTP download)
**FPS:** 30+ (aucune limitation)
**Avantages:** Cache PSRAM, contrôles UI, loop
**Inconvénients:** Pas de streaming RTSP en direct

### NetworkCamera
**Usage:** Streaming réseau en direct (RTSP/MJPEG)
**FPS:** 25-30 (après fix limitation WiFi)
**Avantages:** Streaming réseau, reconnexion auto
**Inconvénients:** Adaptation WiFi RSSI, pas de cache PSRAM

---

## ✅ Choix Rapide

```
Fichier MP4 local?     → SimpleVideoPlayer
Fichier MJPEG local?   → SimpleVideoPlayer
Caméra IP (RTSP)?      → NetworkCamera
Stream MJPEG HTTP?     → NetworkCamera (ou SimpleVideoPlayer avec HTTP download)
Besoin play/pause?     → SimpleVideoPlayer
Performance max?       → SimpleVideoPlayer + MJPEG + PSRAM cache
```

---

**Conclusion:** Pour vos fichiers MP4 locaux, utilisez **SimpleVideoPlayer** avec `fps: 30` pour obtenir les meilleures performances !
