# Optimisations ESP32-P4 pour décodage H.264
# Basé sur https://github.com/espressif/esp-h264-component/issues/5

## 🚀 Performance attendue avec optimisations

| Résolution | FPS décodage | Performance |
|------------|--------------|-------------|
| 320x240    | 151.6 FPS    | ⭐⭐⭐⭐⭐ Excellent |
| 640x480    | 35.7 FPS     | ⭐⭐⭐⭐ Très bon |
| 1024x600   | 16.9 FPS     | ⭐⭐⭐ Bon |

---

## 1️⃣ Activer le décodage Dual-Task (IMPORTANT !)

### Méthode A : Via menuconfig ESPHome

Si vous compilez avec ESPHome, ajoutez dans votre YAML :

```yaml
esphome:
  platformio_options:
    build_flags:
      - -DCONFIG_ESP_H264_DUAL_TASK=1
      - -DCONFIG_ESP_H264_DUAL_TASK_CORE=1
      - -DCONFIG_ESP_H264_DUAL_TASK_PRIORITY=5
```

### Méthode B : Via sdkconfig.defaults

Créez/modifiez `sdkconfig.defaults` :

```ini
# Dual-task decoding (30-50% performance boost)
CONFIG_ESP_H264_DUAL_TASK=y
CONFIG_ESP_H264_DUAL_TASK_CORE=1
CONFIG_ESP_H264_DUAL_TASK_PRIORITY=5
```

---

## 2️⃣ Conversion couleur YUV→RGB optimisée

### Option A : PPA (Hardware - RECOMMANDÉ)

PPA = Pixel Processing Accelerator (accélération matérielle)

**Avantages** :
- ✅ 3-5x plus rapide que software
- ✅ Zero charge CPU
- ✅ Disponible sur ESP32-P4

**Référence d'implémentation** :
```
esp-gmf/elements/gmf_video/esp_gmf_video_ppa.h
```

### Option B : esp-image-effects

Si PPA n'est pas disponible :
- Version SIMD optimisée
- Plus rapide que conversion naïve
- Voir: https://github.com/espressif/esp-image-effects

---

## 3️⃣ Paramètres FFmpeg CRITIQUES

Ces paramètres sont **vérifiés et validés** par Espressif pour ESP32-P4 :

```bash
ffmpeg -i input.mp4 \
  -c:v libx264 \
  -profile:v baseline \          # OBLIGATOIRE
  -preset veryslow \               # Meilleure compression
  -tune fastdecode \               # Optimisé pour décodage rapide
  -vf "format=yuv420p" \           # Format pixel explicite
  -colorspace:v bt709 \            # Standard HD
  -color_primaries:v bt709 \
  -color_trc:v bt709 \
  -color_range:v tv \
  -x264opts slices=1 \             # ⭐ CRITIQUE ! 1 slice par frame
  -g 15 \                          # GOP size
  -bf 0 \                          # Pas de B-frames (Baseline)
  -an \                            # Pas d'audio (ou selon besoin)
  output.h264
```

### ⚠️ Paramètre le plus important : `-x264opts slices=1`

Le décodeur tinyh264 peut avoir des problèmes avec les **multi-slices**. Forcer 1 slice par frame résout beaucoup de problèmes de décodage !

---

## 4️⃣ Tests de validation

### Vérifier le profil et les slices

```bash
# Vérifier profil
ffprobe -v error -select_streams v:0 -show_entries stream=profile \
  -of default=noprint_wrappers=1:nokey=1 video.mp4
# Doit afficher: "Constrained Baseline"

# Compter les slices (devrait être 1 par frame)
ffprobe -v error -select_streams v:0 -show_entries frame=pict_type \
  -of default=noprint_wrappers=1 video.mp4 | head -20
```

### Tester sur ESP32

Logs attendus avec optimisations :

```
[I][H264_DEC.SW]: tinyh264 version: 1.1.1
[I][H264_DEC.SW]: H.264 Decoder initialized (tinyh264/h264bsd supports Baseline profile)
[I][H264_DEC.SW]: Frame decoded: 640x480, size=460800 bytes
[D][H264_DEC]: Decode time: ~25ms (40 FPS capable)
```

---

## 5️⃣ Limitations connues

1. **Profil Baseline uniquement**
   - Main/High profile → erreur "profile_idc is error"
   - Solution : Transcoder en Baseline (go2rtc, FFmpeg)

2. **PTS/DTS**
   - Dépendent du conteneur MP4
   - Raw H.264 (.h264) n'a pas de timestamps
   - Simple video player peut gérer les deux

3. **Résolution maximale**
   - Théorique : 36864 macroblocks (jusqu'à 8K)
   - Pratique : Limité par RAM (640x480 recommandé pour fluidité)

---

## 📊 Comparaison performance

| Configuration | 640x480 @ 15 FPS | Notes |
|--------------|------------------|-------|
| **Sans optimisations** | ~10-15 FPS | Saccadé |
| **+ Dual-task** | ~25-30 FPS | Fluide |
| **+ Dual-task + PPA** | **35-40 FPS** | ⭐ **Optimal** |

---

## 🔗 Références

- Issue originale : https://github.com/espressif/esp-h264-component/issues/5
- esp-gmf (PPA example) : https://github.com/espressif/esp-adf/tree/master/components/esp-gmf
- esp-image-effects : https://github.com/espressif/esp-image-effects

---

## ✅ Checklist d'optimisation

- [ ] CONFIG_ESP_H264_DUAL_TASK activé
- [ ] Vidéos converties avec `-x264opts slices=1`
- [ ] Colorimétrie BT.709 configurée
- [ ] Format YUV420p explicite
- [ ] PPA activé pour conversion couleur (si disponible)
- [ ] Tests de performance effectués

Avec toutes ces optimisations, votre ESP32-P4 devrait décoder **fluide à 35-40 FPS en 640x480 !** 🚀
