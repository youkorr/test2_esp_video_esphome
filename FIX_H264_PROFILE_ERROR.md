# Fix H264 Profile Error - Tapo Camera

## Symptômes

```
[I][network_camera:074]: Network Camera display started
[I][network_camera:861]: Cached SPS: 24 bytes
[I][network_camera:875]: Cached PPS: 8 bytes
[E][H264_DEC]: profile_idc is error
[E][H264_DEC]: Decode sequence parameter set error.
[E][H264_DEC.SW]: Error in decoding
```

**Diagnostic** : ✅ Connexion RTSP fonctionne, ❌ Décodeur H264 rejette le profil vidéo

---

## Cause

Votre caméra Tapo utilise probablement **High Profile H264** que le décodeur OpenH264 de l'ESP32-P4 ne supporte pas correctement.

**Profils H264 supportés par ESP32-P4** :
- ✅ **Baseline Profile** (profile_idc: 66)
- ✅ **Main Profile** (profile_idc: 77)
- ⚠️ **High Profile** (profile_idc: 100) - support partiel
- ❌ **High 10 Profile** et au-delà

---

## Solution 1 : Utiliser stream2 (RECOMMANDÉ)

### Changez l'URL dans p4mini.yaml :

```yaml
network_camera:
  - id: security_cam_1
    url: "rtsp://Tapoone:Tapoone132@192.168.1.56:554/stream2"  # ← stream2 au lieu de stream1
    protocol: rtsp
    width: 320
    height: 240
    canvas_id: security_canvas
    update_interval: 100ms
```

### Pourquoi stream2 ?

Les caméras Tapo ont généralement 2 flux :
- **stream1** : Haute qualité (1080p, High Profile) → Ne fonctionne pas
- **stream2** : Qualité moyenne (360p-720p, Main/Baseline Profile) → Fonctionne ✅

---

## Solution 2 : Configurer la caméra dans l'app Tapo

1. Ouvrez l'application **Tapo** sur votre smartphone
2. Sélectionnez votre caméra (192.168.1.56)
3. Allez dans **Paramètres** (icône engrenage)
4. Cherchez **Paramètres vidéo** ou **Qualité d'enregistrement**
5. Changez :
   - **Résolution** : 720p ou moins (pas 1080p)
   - **Profil d'encodage** : Main ou Baseline (si disponible)
   - **Bitrate** : Réduire à 1-2 Mbps

---

## Solution 3 : Diagnostiquer avec ffprobe

### Vérifiez quel profil H264 votre caméra utilise :

```bash
# Vérifier stream1
ffprobe -v error -select_streams v:0 \
  -show_entries stream=codec_name,profile,level,width,height \
  rtsp://Tapoone:Tapoone132@192.168.1.56:554/stream1

# Vérifier stream2
ffprobe -v error -select_streams v:0 \
  -show_entries stream=codec_name,profile,level,width,height \
  rtsp://Tapoone:Tapoone132@192.168.1.56:554/stream2
```

### Interprétation :

| Profil affiché | Compatible ESP32-P4 ? | Action |
|----------------|----------------------|--------|
| Baseline | ✅ OUI | Utilisez ce flux |
| Main | ✅ OUI | Utilisez ce flux |
| High | ⚠️ PARTIEL | Essayez, sinon utilisez autre flux |
| High 10 | ❌ NON | Changez les paramètres caméra |

---

## Solution 4 : Utiliser go2rtc comme proxy (Avancé)

Si vous avez **Home Assistant** ou **Frigate**, configurez go2rtc pour transcoder :

### Dans go2rtc.yaml :

```yaml
streams:
  tapo_cam1_baseline:
    - rtsp://Tapoone:Tapoone132@192.168.1.56:554/stream1
    - "ffmpeg:tapo_cam1_baseline#video=h264#profile=baseline#width=640#height=360"
```

### Dans p4mini.yaml :

```yaml
network_camera:
  - id: security_cam_1
    url: "rtsp://votre-serveur-homeassistant:8554/tapo_cam1_baseline"
    protocol: rtsp
    width: 640
    height: 360
```

---

## Solution 5 : Vérifier les autres URLs RTSP Tapo

Essayez ces variantes d'URL :

```yaml
# Variante 1 - Stream basse qualité
url: "rtsp://Tapoone:Tapoone132@192.168.1.56:554/stream2"

# Variante 2 - Sans sous-chemin
url: "rtsp://Tapoone:Tapoone132@192.168.1.56:554/"

# Variante 3 - Port 8554 (si go2rtc installé sur la caméra)
url: "rtsp://Tapoone:Tapoone132@192.168.1.56:8554/stream1"

# Variante 4 - Onvif stream
url: "rtsp://Tapoone:Tapoone132@192.168.1.56:554/onvif1"
```

---

## Checklist de dépannage

- [ ] Essayé `stream2` au lieu de `stream1`
- [ ] Vérifié les paramètres vidéo dans l'app Tapo
- [ ] Testé avec ffprobe pour voir le profil H264
- [ ] Essayé de réduire la résolution dans p4mini.yaml (width/height)
- [ ] Vérifié que la caméra est sur le même réseau local
- [ ] Essayé de redémarrer la caméra Tapo

---

## Logs à surveiller après le changement

### ✅ Succès attendu :

```
[I][network_camera:074]: Network Camera display started
[I][network_camera:861]: Cached SPS: 24 bytes
[I][network_camera:875]: Cached PPS: 8 bytes
[I][network_camera:925]: Prepended SPS+PPS to I-frame
[I][network_camera:XXX]: Frame decoded successfully  ← Vous devriez voir ça!
```

### ❌ Échec (même erreur) :

```
[E][H264_DEC]: profile_idc is error
```
→ Essayez la solution suivante

---

## Résumé des actions immédiates

**ACTION 1** : Changez `stream1` → `stream2` dans p4mini.yaml

**ACTION 2** : Si ça ne fonctionne toujours pas, réduisez la résolution :
```yaml
width: 640  # au lieu de 320
height: 360 # au lieu de 240
```

**ACTION 3** : Vérifiez dans l'app Tapo et baissez la qualité du stream

---

## Références

- [ESP32-P4 H264 Decoder Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/multimedia/esp_h264.html)
- [OpenH264 Supported Profiles](https://github.com/cisco/openh264/wiki)
- [Tapo Camera RTSP Streams Guide](https://github.com/home-assistant/core/issues/50751)

---

## Besoin d'aide supplémentaire ?

Si aucune solution ne fonctionne, partagez les logs complets avec :
- Le résultat de `ffprobe` sur stream1 et stream2
- Les logs ESP32 complets lors de la connexion
- Le modèle exact de votre caméra Tapo (C100, C200, C310, etc.)
