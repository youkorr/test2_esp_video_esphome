# Diagnostic de performance - Simple Video Player

## Problème reporté
- **Vitesse** : ~0.7 FPS (très lent!)
- **Couleurs** : Incorrectes (FIXÉ dans commit 6392186)

## Points à vérifier

### 1. Le converter YUV→RGB est-il utilisé?

**Log attendu au démarrage** :
```
[I][yuv_rgb:37] YUV→RGB conversion initialized (BT.601 colorspace)
```

**Si absent** → Le converter n'est PAS initialisé → Utilise fallback lent

### 2. Erreur dans convert_i420_to_rgb565_() ?

**Chercher dans les logs** :
```
[E][simple_video_player:1375] YUV converter not initialized!
```

**Si présent** → Le converter est nullptr, fallback inexistant = crash ou très lent

### 3. Résolution de la vidéo

**0.7 FPS peut être causé par** :
- Résolution trop élevée (>640x480)
- Bitrate trop élevé
- Fichier non optimisé (High profile au lieu de Baseline)

**Vérifier avec ffprobe** :
```bash
ffprobe -v error -show_entries stream=codec_name,profile,width,height,r_frame_rate,bit_rate video.mp4
```

### 4. Paramètres FFmpeg manquants

**Vérifier que la vidéo a** :
- `profile=Baseline` (pas High/Main)
- `slices=1` (CRITIQUE pour tinyh264)
- Résolution ≤ 640x480
- Bitrate ≤ 500kbps

### 5. Timing logs dans le code

**Activer les logs de profiling** :

Dans `simple_video_player.cpp`, ligne ~2276, chercher :
```cpp
// if (decode_time > 100) {  ← Décommenter pour voir timing
//   ESP_LOGI(TAG, "H.264 decode time: %lu ms", decode_time);
// }
```

**Décommenter ces lignes** pour voir le temps de décodage H.264.

### 6. Vérifier l'ancien code vs nouveau

**Test rapide** : Commenter temporairement l'utilisation du nouveau converter :

```cpp
void SimpleVideoPlayer::convert_i420_to_rgb565_(...) {
  // TEMPORAIRE : forcer l'ancien code
  /* if (this->yuv_converter_ != nullptr) {
    this->yuv_converter_->convert_i420_to_rgb565(yuv, rgb, w, h);
  } else */ {
    // Code naïf pour comparaison
    const uint8_t *y_plane = yuv;
    const uint8_t *u_plane = yuv + w * h;
    const uint8_t *v_plane = u_plane + (w * h / 4);
    uint16_t *rgb565 = (uint16_t *)rgb;

    for (int j = 0; j < h; j++) {
      for (int i = 0; i < w; i++) {
        int y_val = y_plane[j * w + i];
        int u_val = u_plane[(j/2) * (w/2) + (i/2)];
        int v_val = v_plane[(j/2) * (w/2) + (i/2)];

        int c = y_val - 16;
        int d = u_val - 128;
        int e = v_val - 128;

        int r = (298 * c + 409 * e + 128) >> 8;
        int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
        int b = (298 * c + 516 * d + 128) >> 8;

        lv_color_t color = lv_color_make(
          (r < 0) ? 0 : ((r > 255) ? 255 : r),
          (g < 0) ? 0 : ((g > 255) ? 255 : g),
          (b < 0) ? 0 : ((b > 255) ? 255 : b)
        );
        rgb565[j * w + i] = color.full;
      }
    }
  }
}
```

**Si même vitesse** → Le problème n'est PAS la conversion YUV→RGB.
**Si plus rapide** → Le nouveau converter a un bug.

## Mesures de performance attendues

### Avec optimisations (BT.601 lookup tables)
- 640x480 : **25-30 FPS**
- 480x272 : **40-50 FPS**

### Sans optimisations (boucle naïve)
- 640x480 : **5-10 FPS**
- 480x272 : **15-20 FPS**

### Vitesse actuelle (0.7 FPS)
**Causes possibles** :
1. ❌ Vidéo en High/Main profile (pas Baseline) → Échec de décodage, retry lent
2. ❌ Résolution >1280x720 → Trop de données
3. ❌ Bitrate >1Mbps → I/O disque lent
4. ❌ Fichier non optimisé avec `-movflags +faststart`
5. ❌ Converter non utilisé + autre goulot

## Actions recommandées

### 1. Vérifier les logs
Chercher :
```
[I][yuv_rgb:37] YUV→RGB conversion initialized (BT.601 colorspace)
[I][simple_video_player:614] H.264 decoder initialized for 640x480
```

### 2. Tester avec vidéo optimale
Utiliser le script de conversion :
```bash
./convert_movie_esp32p4_optimized.sh test.mp4 test_esp32.mp4 480:272
```

Résolution 480x272 + Baseline + slices=1 devrait donner **40-50 FPS**.

### 3. Activer profiling détaillé
Dans `simple_video_player.cpp` :
- Décommenter les logs de timing (ligne ~2276)
- Activer logs DEBUG :
  ```yaml
  logger:
    level: DEBUG
    logs:
      simple_video_player: VERBOSE
  ```

### 4. Mesurer chaque étape
Ajouter timing dans `convert_i420_to_rgb565_()` :
```cpp
uint32_t t1 = esp_timer_get_time();
this->yuv_converter_->convert_i420_to_rgb565(yuv, rgb, w, h);
uint32_t t2 = esp_timer_get_time();
ESP_LOGI(TAG, "YUV→RGB: %u us for %dx%d", (t2-t1), w, h);
```

**Temps attendu** :
- 640x480 avec LUT : **~3-5 ms** (3000-5000 us)
- 640x480 sans LUT : **~30-40 ms**

Si vous voyez >50ms → Il y a un problème.

---

**Envoyez-moi vos logs et je pourrai diagnostiquer précisément !**
