# Guide de dépannage RTSP - frigate2

## 🔴 Problème détecté

Logs ESP32:
```
[05:39:27][I][rtsp_server:447]: Session 4FB2830C setup, client RTP port: 0
[05:39:27][I][rtsp_server:488]: Session 4FB2830C teardown
```

**Analyse**: Le client (Frigate via go2rtc) envoie `client RTP port: 0`, ce qui est invalide. La session se termine immédiatement.

## 🔍 Causes possibles

1. **go2rtc ne gère pas bien le H.264 hardware de l'ESP32**
2. **Input args incorrects dans Frigate** (preset-rtsp-restream vs preset-rtsp-generic)
3. **Problème de transcoding dans go2rtc**

## ✅ Solutions à tester

### Solution 1: Stream DIRECT (sans go2rtc)

**Changez la config frigate2 pour utiliser le stream direct:**

```yaml
cameras:
  frigate2:
    ffmpeg:
      inputs:
        - path: rtsp://192.168.1.49:554/stream  # ✅ Direct depuis ESP32
          input_args: preset-rtsp-generic       # ✅ Changé de preset-rtsp-restream
          roles:
            - detect
    detect:
      enabled: true
      width: 800
      height: 640
      fps: 30
```

**Puis redémarrez Frigate:**
```bash
docker restart frigate
```

**Vérifiez les logs:**
```bash
docker logs -f frigate | grep frigate2
```

**Attendu:**
```
[frigate2] frigate2: 30.0 FPS (30 ms)
```

---

### Solution 2: Fix go2rtc (si vous voulez le re-streaming)

Si le stream direct fonctionne mais vous voulez utiliser go2rtc, modifiez la config go2rtc:

```yaml
go2rtc:
  streams:
    frigate2:
      # Option A: Stream direct sans transcoding
      - rtsp://192.168.1.49:554/stream

      # Option B: Avec transcoding explicite (si Option A ne fonctionne pas)
      # - ffmpeg:rtsp://192.168.1.49:554/stream#video=h264#hardware

      # ❌ Retirez cette ligne si elle cause des problèmes:
      # - ffmpeg:frigate2#audio=aac
```

**Testez go2rtc manuellement:**

```bash
# Dans le conteneur Frigate, testez go2rtc
docker exec -it frigate bash

# Testez le stream go2rtc
ffplay -rtsp_transport tcp rtsp://127.0.0.1:8554/frigate2

# Ou avec curl pour voir les streams disponibles
curl http://127.0.0.1:1984/api/streams
```

---

### Solution 3: Arguments d'entrée différents

Si ni Solution 1 ni Solution 2 ne fonctionnent, essayez différents presets:

```yaml
cameras:
  frigate2:
    ffmpeg:
      inputs:
        - path: rtsp://192.168.1.49:554/stream
          # Essayez ces presets dans l'ordre:
          input_args: preset-rtsp-generic     # ← Commencez par celui-ci
          # input_args: preset-rtsp-udp       # Si generic ne fonctionne pas
          # input_args: preset-rtsp-blue-iris # Alternative
          # input_args:                       # Ou définissez manuellement:
          #   - -avoid_negative_ts
          #   - make_zero
          #   - -fflags
          #   - nobuffer
          #   - -flags
          #   - low_delay
          #   - -strict
          #   - experimental
          #   - -fflags
          #   - +genpts+discardcorrupt
          #   - -rtsp_transport
          #   - tcp
          #   - -timeout
          #   - "5000000"
          #   - -use_wallclock_as_timestamps
          #   - "1"
```

---

## 🧪 Tests de validation

### Test 1: Stream ESP32 fonctionne?

```bash
# Depuis votre machine
ffplay -rtsp_transport tcp rtsp://192.168.1.49:554/stream

# Ou avec VLC
vlc rtsp://192.168.1.49:554/stream
```

**Si ça fonctionne**: Le problème est dans Frigate/go2rtc, pas dans l'ESP32.

### Test 2: go2rtc fonctionne?

```bash
# Vérifiez que go2rtc tourne
docker exec frigate curl http://localhost:1984/api/streams

# Devrait montrer:
# {
#   "frigate1": {...},
#   "frigate2": {...}
# }
```

### Test 3: Logs go2rtc

```bash
# Logs détaillés
docker logs frigate 2>&1 | grep -i go2rtc

# Cherchez des erreurs comme:
# [go2rtc] ERROR can't create producer
# [go2rtc] ERROR rtsp connection failed
```

---

## 📊 Comparaison des approches

| Approche | Latence | CPU | Compatibilité | Recommandé pour |
|----------|---------|-----|---------------|-----------------|
| **Direct RTSP** | Très faible | Faible | ✅ Haute | Production |
| **Via go2rtc** | Faible | Moyenne | ⚠️ Variable | Multi-accès |
| **go2rtc + transcode** | Moyenne | Haute | ✅ Haute | Compatibilité max |

---

## 🎯 Recommandation

**Pour démarrer rapidement:**
1. Utilisez **stream DIRECT** (Solution 1)
2. Une fois que ça fonctionne, testez go2rtc si besoin

**Configuration recommandée (stream direct):**

```yaml
cameras:
  frigate2:
    ffmpeg:
      inputs:
        - path: rtsp://192.168.1.49:554/stream
          input_args: preset-rtsp-generic
          roles:
            - detect
            - record  # Ajoutez record si vous voulez enregistrer

      # Si vous avez un Intel CPU avec GPU intégré
      hwaccel_args: preset-vaapi

    detect:
      enabled: true
      width: 800
      height: 640
      fps: 30

    objects:
      track:
        - person
        - dog
        - cat
      filters:
        person:
          min_score: 0.7
          threshold: 0.8

    # Optionnel: Enregistrement
    record:
      enabled: true
      retain:
        days: 7
        mode: motion
```

---

## 📝 Logs à vérifier

**Après redémarrage de Frigate, vérifiez:**

### Logs ESP32 (ESPHome)
```
[I][rtsp_server:452]: Initializing H.264 encoder (first client)...
[I][rtsp_server:074]: Initializing H.264 hardware encoder...
[I][rtsp_server:148]: H.264 hardware encoder initialized successfully
[I][rtsp_server:447]: Session XXXXXXXX setup, client RTP port: XXXXX  # ← Devrait être > 0
[I][rtsp_server:476]: Session XXXXXXXX started playing
```

### Logs Frigate
```bash
docker logs -f frigate | grep -E "frigate2|ERROR"
```

**Bon signe:**
```
[frigate2] frigate2: 30.0 FPS (30 ms)
[detector.coral] coral: 7.3 FPS (135.0 ms)
```

**Mauvais signe:**
```
[frigate2] ffmpeg process is not running
[frigate2] Unable to read frames from ffmpeg process
```

---

## 🆘 Si rien ne fonctionne

1. **Partagez les logs complets:**
   ```bash
   # Logs ESP32
   # Copiez la sortie de ESPHome

   # Logs Frigate
   docker logs frigate 2>&1 | grep -A 10 -B 10 frigate2 > frigate2_debug.log
   ```

2. **Testez avec FFmpeg directement:**
   ```bash
   ffmpeg -rtsp_transport tcp -i rtsp://192.168.1.49:554/stream \
          -f null - \
          -loglevel debug
   ```

3. **Vérifiez le réseau:**
   ```bash
   # Ping ESP32
   ping 192.168.1.49

   # Telnet sur port RTSP
   telnet 192.168.1.49 554
   ```
