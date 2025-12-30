# ========================================================================
# DIAGNOSTIC: "No H264 frames decoded yet"
# ========================================================================
# Symptôme: Connexion RTSP OK, mais aucune image affichée
# ========================================================================

## Logs observés :

✅ RTSP connexion réussie
✅ SPS/PPS reçus (26+8 bytes)
✅ Stream connecté
❌ No H264 frames decoded yet (500 attempts)

## Cause probable :

Le décodeur H264 **reçoit** les données mais ne peut pas les **décoder**.
Cela signifie que **stream2 utilise AUSSI un profil H264 incompatible**.

---

## 🔍 Diagnostic 1 : Vérifier le profil H264 de stream2

Utilisez `ffprobe` pour voir exactement quel profil votre caméra utilise :

```bash
# Analyser stream2
ffprobe -v error -select_streams v:0 \
  -show_entries stream=codec_name,profile,level,width,height,pix_fmt \
  -of default=noprint_wrappers=1 \
  rtsp://Tapoone:Tapoone132@192.168.1.56:554/stream2
```

### Résultat attendu :

```
codec_name=h264
profile=Main          ← Doit être Baseline, Main, ou Constrained Baseline
level=31
width=640
height=360
pix_fmt=yuv420p
```

### Profils compatibles ESP32-P4 :

| Profil | Compatible ? | profile_idc |
|--------|--------------|-------------|
| Constrained Baseline | ✅ OUI | 66 |
| Baseline | ✅ OUI | 66 |
| Main | ✅ OUI | 77 |
| High | ❌ PARTIEL (problèmes) | 100 |
| High 10 | ❌ NON | 110 |

Si stream2 montre **High Profile**, c'est le problème !

---

## 🔧 Solution 1 : Essayer différentes résolutions

Parfois, changer la résolution dans la configuration force la caméra à utiliser un profil plus simple :

```yaml
network_camera:
  - id: security_cam_1
    url: "rtsp://Tapoone:Tapoone132@192.168.1.56:554/stream2"
    protocol: rtsp
    width: 640   # Essayez différentes tailles
    height: 360  # 640x360, 480x270, 320x180
    update_interval: 100ms
```

**Résolutions à essayer** (de la plus haute à la plus basse) :
- 640x360
- 480x270
- 320x240 ← Actuel
- 320x180

---

## 🔧 Solution 2 : Utiliser MJPEG au lieu de RTSP

Si votre caméra Tapo supporte MJPEG, essayez :

```yaml
network_camera:
  - id: security_cam_1
    # Essayez ces URLs MJPEG possibles :
    url: "http://Tapoone:Tapoone132@192.168.1.56:2020/stream2"  # Port 2020 courant
    # OU
    # url: "http://Tapoone:Tapoone132@192.168.1.56/video/mjpeg.cgi"
    # OU
    # url: "http://Tapoone:Tapoone132@192.168.1.56:8080/video"
    protocol: mjpeg
    width: 320
    height: 240
    update_interval: 100ms
```

**Note** : Toutes les caméras Tapo ne supportent pas MJPEG.

---

## 🔧 Solution 3 : Utiliser go2rtc pour transcoder (RECOMMANDÉ)

Si vous avez Home Assistant avec Frigate ou go2rtc installé :

### Configuration go2rtc (go2rtc.yaml) :

```yaml
streams:
  tapo_cam_baseline:
    - rtsp://Tapoone:Tapoone132@192.168.1.56:554/stream1
    # Transcoder vers Baseline Profile avec FFmpeg
    - ffmpeg:rtsp://Tapoone:Tapoone132@192.168.1.56:554/stream1#video=h264#profile=baseline#width=640#height=360
```

### Configuration ESP32 (p4mini.yaml) :

```yaml
network_camera:
  - id: security_cam_1
    url: "rtsp://192.168.1.XXX:8554/tapo_cam_baseline"  # IP de votre serveur go2rtc
    protocol: rtsp
    width: 640
    height: 360
    update_interval: 100ms
```

**Avantages** :
- ✅ Force le profil Baseline (compatible ESP32-P4)
- ✅ Peut réduire la résolution
- ✅ Peut ajuster le bitrate
- ✅ Peut convertir le framerate

---

## 🔧 Solution 4 : Vérifier les autres streams Tapo

Certaines caméras Tapo ont des URLs alternatives :

```yaml
# Essayez ces variantes :
url: "rtsp://Tapoone:Tapoone132@192.168.1.56:554/stream3"     # Stream basse qualité
url: "rtsp://Tapoone:Tapoone132@192.168.1.56:554/live"       # Stream live
url: "rtsp://Tapoone:Tapoone132@192.168.1.56:8554/unicast"   # Port alternatif
url: "rtsp://Tapoone:Tapoone132@192.168.1.56/h264_stream"    # Format alternatif
```

---

## 🔧 Solution 5 : Logs de debug améliorés

J'ai ajouté des logs de debug dans le code pour voir l'erreur exacte du décodeur.

**Recompilez** avec le nouveau code et vous verrez :

```
[E][network_camera]: H264 decode error: -1 (NAL size: 1234 bytes, total errors: 1)
```

Le code d'erreur vous dira exactement ce qui ne va pas.

---

## 🎯 Plan d'action recommandé :

### Étape 1 : Diagnostic avec ffprobe
```bash
ffprobe -v error -select_streams v:0 -show_entries stream=profile \
  rtsp://Tapoone:Tapoone132@192.168.1.56:554/stream2
```

### Étape 2 : Si profil = High
→ Utilisez go2rtc pour transcoder vers Baseline

### Étape 3 : Si pas de go2rtc
→ Essayez MJPEG (si supporté)

### Étape 4 : Si MJPEG pas supporté
→ Essayez différentes résolutions (640x360, 480x270, 320x180)

### Étape 5 : Recompilez avec les nouveaux logs
→ Pour voir exactement quelle erreur le décodeur retourne

---

## 📊 Table de compatibilité Tapo

| Modèle | stream1 Profile | stream2 Profile | MJPEG Support |
|--------|----------------|-----------------|---------------|
| C100 | High | Main | ❌ Non |
| C200 | High | Main | ❌ Non |
| C210 | High | Main/Baseline | ❌ Non |
| C310 | High | Main | ⚠️ Partiel |
| C320WS | High | Baseline | ✅ Oui (port 2020) |

**Note** : Ces informations sont basées sur des rapports utilisateurs et peuvent varier selon le firmware.

---

## 🆘 Si rien ne fonctionne

1. **Vérifiez le modèle** de votre caméra Tapo (C100, C200, C310, etc.)
2. **Vérifiez la version du firmware** dans l'app Tapo
3. **Essayez de mettre à jour** le firmware de la caméra
4. **Configurez go2rtc** - C'est vraiment la solution la plus fiable !

---

## 📝 Prochaines étapes pour vous

1. **Recompilez** avec les nouveaux logs de debug
2. **Testez** et partagez les logs d'erreur H264 exacts
3. **Lancez** `ffprobe` sur stream2 pour voir le profil
4. On ajustera la solution en fonction des résultats !

