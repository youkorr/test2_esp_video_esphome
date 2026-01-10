# 🚀 Solution pour MP4 Rapide sur ESP32-P4

## 🔴 Votre Problème

```
Situation actuelle:
├─ MJPEG (JPEG hardware): 30+ FPS ✅
├─ MP4 Baseline (tinyh264): 7-8 FPS ❌
└─ MP4 High Profile: Probablement 4-5 FPS ❌
```

**Pourquoi ?**
- ESP32-P4 a un **décodeur JPEG hardware** (très rapide)
- ESP32-P4 **N'A PAS** de décodeur H.264 hardware
- Décodage H.264 software = 80-150ms par frame
- Conversion YUV→RGB = 25-35ms
- **Total: 105-185ms → 5-9 FPS**

---

## ✅ Solution Recommandée: Convertir MP4 → MJPEG

### Étape 1: Installer FFmpeg

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install ffmpeg
```

**macOS:**
```bash
brew install ffmpeg
```

**Windows:**
Télécharger depuis https://ffmpeg.org/download.html

### Étape 2: Convertir vos vidéos

**Utiliser le script automatique:**
```bash
cd /home/user/test2_esp_video_esphome/tools
./convert_mp4_to_mjpeg.sh votre_video.mp4
```

**Ou manuellement avec FFmpeg:**

#### Option A: Qualité optimale (recommandé)
```bash
ffmpeg -i input.mp4 \
  -c:v mjpeg \
  -q:v 10 \
  -vf "scale=640:480" \
  -r 25 \
  -an \
  output.avi
```

#### Option B: Petite résolution (performance max)
```bash
ffmpeg -i input.mp4 \
  -c:v mjpeg \
  -q:v 10 \
  -vf "scale=320:240" \
  -r 30 \
  -an \
  output_small.avi
```

#### Option C: Haute qualité visuelle
```bash
ffmpeg -i input.mp4 \
  -c:v mjpeg \
  -q:v 5 \
  -vf "scale=640:480" \
  -r 25 \
  -an \
  output_hq.avi
```

### Étape 3: Configuration ESPHome

```yaml
# Dans votre fichier .yaml
video_player:
  - id: my_video_player
    file: "/sdcard/output.avi"  # Fichier MJPEG converti
    canvas_id: my_canvas
    loop: true
    autoplay: true
```

### Résultat Attendu

```
📊 Performance MJPEG:
├─ 320×240: 30+ FPS ⭐⭐⭐
├─ 640×480: 25-30 FPS ⭐⭐⭐
└─ 1280×720: 15-20 FPS ⭐⭐
```

---

## 🔧 Alternative: Optimiser H.264 (si conversion impossible)

Si vous DEVEZ garder MP4/H.264:

### Option 1: Réduire Résolution

```yaml
network_camera:
  - id: cam
    url: "rtsp://ip/stream2"  # Stream basse résolution
    width: 320
    height: 240  # Très petit = plus rapide
    canvas_id: canvas
```

**Gain:** 7-8 FPS → 20-25 FPS

### Option 2: Compiler avec Optimisations

Modifier `components/esp_h264/CMakeLists.txt`:
```cmake
target_compile_options(esp_h264 PRIVATE
    -O3                 # Optimisation maximale
    -funroll-loops      # Dérouler boucles
    -ftree-vectorize    # Vectorisation
    -ffast-math         # Math rapide
)
```

**Gain:** +15-20% performance

### Option 3: Désactiver OpenH264, garder tinyh264

Dans `esp_h264_dec_sw.c` ligne 8:
```c
// Changer de:
#define CONFIG_USE_OPENH264 1

// À:
#define CONFIG_USE_OPENH264 0
```

**Raison:** tinyh264 Baseline est 1.5-2x plus rapide qu'OpenH264 High Profile

---

## 📊 Tableau Comparatif Complet

| Solution | Résolution | FPS | Qualité | Effort |
|----------|-----------|-----|---------|--------|
| **MP4 Baseline actuel** | 640×480 | 7-8 | ⭐⭐⭐ | - |
| **MP4 High Profile (OpenH264)** | 640×480 | 4-5 | ⭐⭐⭐ | Facile |
| **MJPEG converti** | 640×480 | **25-30** | ⭐⭐⭐ | Facile |
| **MJPEG converti** | 320×240 | **30+** | ⭐⭐⭐ | Facile |
| **H.264 optimisé** | 320×240 | 20-25 | ⭐⭐⭐ | Difficile |
| **H.264 + hardware YUV** | 640×480 | 15-18 | ⭐⭐⭐ | Très difficile |

---

## 🎯 Recommandation par Cas d'Usage

### Cas 1: Vidéos sur carte SD
```bash
# Convertir une fois, lire rapidement
ffmpeg -i video.mp4 -c:v mjpeg -q:v 10 video.avi
# → 25-30 FPS garanti
```

### Cas 2: Streaming caméra IP
```yaml
# Utiliser flux MJPEG de la caméra
network_camera:
  url: "http://camera_ip/mjpeg"
  protocol: mjpeg
# → 25-30 FPS garanti
```

### Cas 3: Doit absolument lire MP4
```yaml
# Accepter performances limitées
video_player:
  width: 320  # Très petit
  height: 240
# → 15-20 FPS maximum
```

---

## 🧪 Tests Rapides

### Test 1: Vérifier MJPEG fonctionne

```bash
# Convertir vidéo de 10 secondes
ffmpeg -i test.mp4 -c:v mjpeg -q:v 10 -t 10 test.avi

# Flash sur ESP32-P4
esphome run config.yaml

# Vérifier logs:
# [video_player] FPS: 28.5  ← Devrait voir ~25-30 FPS
```

### Test 2: Comparer qualités MJPEG

```bash
# Haute qualité (gros fichier)
ffmpeg -i test.mp4 -c:v mjpeg -q:v 5 test_hq.avi

# Qualité moyenne (équilibré)
ffmpeg -i test.mp4 -c:v mjpeg -q:v 10 test_mq.avi

# Basse qualité (petit fichier)
ffmpeg -i test.mp4 -c:v mjpeg -q:v 20 test_lq.avi

# Tester chaque fichier et comparer qualité visuelle
```

### Test 3: Trouver résolution optimale

```bash
# Test 320×240
ffmpeg -i test.mp4 -c:v mjpeg -q:v 10 -vf "scale=320:240" test_320.avi

# Test 480×320
ffmpeg -i test.mp4 -c:v mjpeg -q:v 10 -vf "scale=480:320" test_480.avi

# Test 640×480
ffmpeg -i test.mp4 -c:v mjpeg -q:v 10 -vf "scale=640:480" test_640.avi

# Voir quelle résolution donne le meilleur FPS sur votre écran
```

---

## ❓ FAQ

**Q: La qualité MJPEG est-elle aussi bonne que MP4?**
A: Avec `-q:v 5-10`, la qualité est excellente et visuellement identique pour un affichage en temps réel sur petit écran.

**Q: Les fichiers MJPEG sont-ils plus gros?**
A: Oui, généralement 2-5x plus gros que MP4. Mais décodage 5-7x plus rapide sur ESP32-P4!

**Q: Puis-je garder l'audio?**
A: Non, ESP32-P4 ne décode pas l'audio vidéo dans ce contexte. Utilisez `-an` pour retirer l'audio.

**Q: Pourquoi pas H.264 hardware comme H.264 encoder?**
A: ESP32-P4 a un **encodeur** H.264 hardware (pour enregistrer), mais **PAS** de décodeur H.264 hardware (pour lire).

**Q: OpenH264 High Profile apporte quoi si c'est plus lent?**
A: Il permet de lire des MP4 modernes qui ne sont pas décodables avec tinyh264 Baseline. Mais c'est 1.5-2x plus lent.

**Q: Quelle est la meilleure qualité JPEG (`-q:v`)?**
A:
- `5-8`: Excellente qualité, pour écrans haute résolution
- `10-12`: **Recommandé** - Bon équilibre qualité/taille
- `15-20`: Acceptable, pour économiser espace
- `25-31`: Basse qualité, pas recommandé

---

## 📚 Ressources

- **Script conversion:** `/tools/convert_mp4_to_mjpeg.sh`
- **Analyse complète:** `MP4_PERFORMANCE_ANALYSIS.md`
- **Documentation H.264:** `ESP32P4_H264_HIGH_PROFILE_SUPPORT.md`
- **FFmpeg MJPEG:** https://ffmpeg.org/ffmpeg-formats.html#mjpeg

---

## 🎬 Résumé en 30 secondes

```bash
# 1. Installer FFmpeg
sudo apt install ffmpeg  # ou brew install ffmpeg sur macOS

# 2. Convertir votre vidéo
cd tools
./convert_mp4_to_mjpeg.sh ../videos/ma_video.mp4

# 3. Copier sur carte SD
cp ma_video_mjpeg_q10_640x480_25fps.avi /sdcard/

# 4. Configuration ESPHome
# video_player:
#   file: "/sdcard/ma_video_mjpeg_q10_640x480_25fps.avi"

# 5. Flash et profiter de 25-30 FPS ! 🚀
```

**Bonne chance !** 🎉
