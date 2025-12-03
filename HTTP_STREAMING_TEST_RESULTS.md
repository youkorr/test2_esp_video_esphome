# Test HTTP Streaming - Résultats Attendus

## ✅ Fix appliqué : Attente de la connexion WiFi

**Problème résolu** : Le lecteur tentait de télécharger AVANT que le WiFi soit connecté.

**Solution** : Le code attend maintenant jusqu'à 30 secondes que le WiFi soit connecté avant de commencer le téléchargement.

## 📊 Logs attendus (SUCCÈS)

Quand vous flashez et testez, vous devriez voir :

```
[I][simple_video_player:035]: Setting up Simple Video Player...
[I][simple_video_player:036]:   File: http://192.168.1.158:8080/kOKLKPxr/Rise-of-Skywalker_fixed.mp4
[I][simple_video_player:377]: Opening HTTP/HTTPS source: http://...
[I][simple_video_player:269]: Downloading from HTTP/HTTPS: http://...
[I][simple_video_player:277]: Waiting for WiFi connection...    ← NOUVEAU!
[I][wifi:1079]: Connected                                        ← WiFi se connecte
[I][simple_video_player:286]: ✓ WiFi connected, starting download...  ← NOUVEAU!
[I][simple_video_player:308]: HTTP file size: 933987 bytes (0.89 MB)
[I][simple_video_player:347]: Download progress: 10% (93398 / 933987 bytes)
[I][simple_video_player:347]: Download progress: 20% (186797 / 933987 bytes)
[I][simple_video_player:347]: Download progress: 30% (280196 / 933987 bytes)
[I][simple_video_player:347]: Download progress: 40% (373594 / 933987 bytes)
[I][simple_video_player:347]: Download progress: 50% (466993 / 933987 bytes)
[I][simple_video_player:347]: Download progress: 60% (560392 / 933987 bytes)
[I][simple_video_player:347]: Download progress: 70% (653790 / 933987 bytes)
[I][simple_video_player:347]: Download progress: 80% (747189 / 933987 bytes)
[I][simple_video_player:347]: Download progress: 90% (840588 / 933987 bytes)
[I][simple_video_player:359]: ✓ HTTP download complete: 933987 bytes
[I][simple_video_player:387]: ✓ HTTP video opened from memory: 933987 bytes
[I][simple_video_player:426]: Video file opened: 933987 bytes
[I][simple_video_player:614]: H.264 decoder initialized for 800x480
[I][yuv_rgb:37]: YUV→RGB conversion initialized (BT.601 colorspace)
```

## 🎯 Différences clés vs version précédente

| Avant | Après |
|-------|-------|
| ❌ "Host is unreachable" | ✅ "Waiting for WiFi connection..." |
| ❌ WiFi connecte APRÈS l'erreur | ✅ Attend le WiFi AVANT de télécharger |
| ❌ Téléchargement échoue | ✅ Téléchargement réussit |

## 🚀 Performance attendue

### Avec SD Card (votre test précédent)
- **Vitesse** : 0.7 FPS
- **Cause** : Carte SD lente (~1-5 MB/s)

### Avec HTTP → SPIRAM (après ce fix)
- **Vitesse attendue** : **25-30 FPS** @ 800x480
- **Gain** : **35-40x plus rapide !**
- **Cause** : Lecture depuis SPIRAM (~50-100 MB/s)

## 📝 Comment tester

1. **Compiler et flasher** le code actuel :
   ```bash
   esphome run esp32-video.yaml
   ```

2. **Ouvrir les logs** :
   ```bash
   esphome logs esp32-video.yaml
   ```

3. **Chercher ces messages** :
   - ✅ "Waiting for WiFi connection..."
   - ✅ "✓ WiFi connected, starting download..."
   - ✅ "Download progress: X%"
   - ✅ "✓ HTTP download complete"
   - ✅ "YUV→RGB conversion initialized (BT.601 colorspace)"

4. **Vérifier l'affichage** :
   - Le lecteur vidéo devrait apparaître sur `video_page`
   - Les contrôles devraient être visibles
   - La vidéo devrait se charger

5. **Mesurer les FPS** :
   - Chercher dans les logs : `"Frame X/XXX"`
   - Chronométrer combien de frames en 10 secondes
   - Calculer : FPS = (nombre_frames / 10)

## ⚠️ Si ça ne fonctionne toujours pas

### Erreur : "WiFi connection timeout (30s)"
**Cause** : Le WiFi ne se connecte pas du tout.

**Vérifier** :
- SSID et mot de passe corrects dans le YAML
- Le routeur WiFi est allumé et accessible
- L'ESP32 est à portée du WiFi

### Erreur : "Failed to allocate ... bytes in SPIRAM"
**Cause** : Pas assez de SPIRAM libre.

**Solution** :
- Utiliser une vidéo plus petite (< 5 MB)
- Vérifier que SPIRAM est bien configuré dans le YAML

### Vidéo ne s'affiche toujours pas
**Vérifier** :
- `parent_id: video_page` existe dans la config LVGL
- Pas de `media_player_entity` (ce paramètre n'existe pas)
- Width et height sont commentés (utilise résolution vidéo par défaut)

## 📊 Tableau de comparaison complet

| Paramètre | SD Card (avant) | HTTP→SPIRAM (après) |
|-----------|----------------|---------------------|
| **FPS mesuré** | 0.7 | **25-30 attendu** |
| **Temps ouverture** | <1s | ~5s (download) |
| **Vitesse lecture** | ~1-5 MB/s | ~50-100 MB/s |
| **Latence frame** | ~1400 ms | ~33-40 ms |
| **Couleurs** | ✅ Correctes (BT.601) | ✅ Correctes (BT.601) |
| **Fiabilité** | Dépend carte SD | ✅ Stable (SPIRAM) |

## ✅ Checklist de validation

- [ ] Logs montrent "Waiting for WiFi connection..."
- [ ] Logs montrent "✓ WiFi connected, starting download..."
- [ ] Download atteint 100%
- [ ] "✓ HTTP download complete" apparaît
- [ ] "H.264 decoder initialized" apparaît
- [ ] "YUV→RGB conversion initialized (BT.601 colorspace)" apparaît
- [ ] Le lecteur vidéo s'affiche sur l'écran
- [ ] Les contrôles sont visibles
- [ ] La vidéo démarre (si auto_play: true)
- [ ] Les couleurs sont correctes
- [ ] **Les FPS sont > 20 FPS**

## 🎉 Résultat attendu final

**Avant** (SD Card) :
- 0.7 FPS = slideshow inutilisable
- Couleurs OK mais trop lent

**Après** (HTTP→SPIRAM) :
- **25-30 FPS = vidéo fluide !** 🚀
- Couleurs correctes (BT.601)
- Lecture stable depuis SPIRAM

---

**Prochaines étapes si succès** :
1. ✅ Confirmer les FPS sont > 20
2. ✅ Tester avec vidéos plus grandes (jusqu'à 10 MB)
3. ✅ Optimiser la compression FFmpeg pour réduire taille
4. 🔧 (Optionnel) Activer `CONFIG_ESP_H264_DUAL_TASK` pour +30-50% perf
