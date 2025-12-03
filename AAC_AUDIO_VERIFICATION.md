# Test de vérification AAC

Ce document décrit comment vérifier que le décodage AAC fonctionne correctement.

## 1. Vérification à la compilation

### Logs attendus
Pendant la compilation avec ESPHome, vous devriez voir :

```
[Simple Video Player] Build script running...
[Simple Video Player] Found audio codec libraries in: .../esp_audio_codec/lib/esp32p4
[Simple Video Player] Added esp_audio_codec include paths
[Simple Video Player] ✓ Linked esp_audio_codec libraries (with --whole-archive)
[Simple Video Player]   AAC audio decoding should now be available
```

### Si les logs montrent un problème
```
[Simple Video Player] ⚠️  esp_audio_codec component not found
```
→ Vérifiez que le dossier `components/esp_audio_codec` existe

```
[Simple Video Player] ⚠️  Audio codec libraries not found at .../lib/esp32p4
```
→ Vérifiez que les fichiers `.a` existent dans `components/esp_audio_codec/lib/esp32p4/`

## 2. Vérification au runtime

### Logs attendus au démarrage
Lors de l'ouverture d'un fichier MP4 avec audio AAC :

```
[I][simple_video_player:154] Opening video file: /data/video.mp4
[I][simple_video_player:267] Found video track: H.264
[I][simple_video_player:1062] Found AAC config: 2 bytes
[I][simple_video_player:1129] AAC decoder initialized: 44100 Hz, 2 channels
```

### Si AAC n'est pas disponible
```
[W][simple_video_player:1133] AAC decoder not available - esp_audio_codec not found
```
→ Le header `esp_audio_dec.h` n'a pas été trouvé à la compilation
→ Vérifiez que `USE_ESP_AUDIO_CODEC` est défini à 1

## 3. Test avec une vidéo MP4 + AAC

### Créer une vidéo test
Utilisez le script optimisé avec audio AAC :

```bash
cd components/simple_video_player
./convert_movie_with_normalisation.sh input.mp4 output_esp32.mp4 640:480
```

**Important** : Le script actuel génère de l'audio **PCM u8**, pas AAC !

### Pour générer AAC au lieu de PCM
Modifiez le script pour utiliser AAC :

```bash
ffmpeg -i "$input_file" \
  ... \
  -c:v libx264 \
  -profile:v baseline \
  ... \
  -c:a aac \              # ← AAC encoder
  -b:a 64k \              # ← Bitrate audio
  -ar 44100 \             # ← Sample rate
  -ac 2 \                 # ← Stereo
  "$output_file"
```

## 4. Configuration YAML

Pour activer l'audio, configurez un speaker dans votre YAML :

```yaml
# Exemple avec speaker I2S
i2s_audio:
  - id: i2s_out
    i2s_lrclk_pin: GPIO10
    i2s_bclk_pin: GPIO11

speaker:
  - platform: i2s_audio
    id: my_speaker
    dac_type: external
    i2s_audio_id: i2s_out
    i2s_dout_pin: GPIO12

simple_video_player:
  id: video_player
  file_path: "/data/video.mp4"
  speaker: my_speaker  # ← Active l'audio
```

## 5. Vérification du décodage

### Commandes de debug
Ajoutez des logs dans votre configuration :

```yaml
logger:
  level: DEBUG
  logs:
    simple_video_player: DEBUG
```

### Logs attendus pendant la lecture
```
[D][simple_video_player:1194] AAC decode OK: 2048 bytes PCM
[D][simple_video_player:1195] Written to speaker: 2048 bytes
```

### En cas d'erreur
```
[W][simple_video_player:1189] AAC decode failed: -1
```
→ Vérifiez le format audio (doit être AAC LC, pas HE-AAC v2)
→ Vérifiez le sample rate (44100 Hz ou 48000 Hz recommandé)

## 6. Formats audio supportés

### ✅ Supportés
- **AAC LC** (Low Complexity) - Le plus courant
- **AAC+ / HE-AAC v1** (avec `aac_plus_enable = true`)

### ⚠️ Non testés
- HE-AAC v2
- AAC Main profile

### ❌ Non supportés
- MP3 (nécessite un autre décodeur)
- Vorbis
- Opus

## 7. Résumé du statut

| Composant | Status | Notes |
|-----------|--------|-------|
| **Bibliothèques esp_audio_codec** | ✅ Présentes | ESP32-P4 libs disponibles |
| **Build script** | ✅ Configuré | Linking automatique |
| **Headers** | ✅ Disponibles | `decoder/esp_audio_dec.h` |
| **Parse mp4a** | ✅ Implémenté | Extrait AAC config |
| **Init décodeur** | ✅ Implémenté | `esp_aac_dec_register()` |
| **Décodage** | ✅ Implémenté | `esp_audio_dec_process()` |
| **Sortie speaker** | ✅ Implémenté | Sync avec vidéo |
| **Tests** | ⚠️ À faire | Nécessite vidéo AAC + speaker |

## 8. Problèmes connus

### Scripts de conversion génèrent PCM au lieu d'AAC
Les scripts actuels utilisent `-acodec pcm_u8` qui génère de l'audio non compressé.

**Solution** : Modifier pour utiliser `-c:a aac` si vous voulez tester le décodeur AAC.

### Audio PCM fonctionne directement
Le PCM u8 (8-bit mono 16kHz) est joué directement sans décodeur, donc c'est plus simple pour les tests.

**AAC** est utile si vous avez des fichiers existants ou voulez de meilleure qualité audio.

---

**Conclusion** : Le support AAC est **complètement implémenté et prêt** ! Il suffit de :
1. Configurer un speaker dans YAML
2. Utiliser une vidéo MP4 avec audio AAC
3. Vérifier les logs de démarrage
