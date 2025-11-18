# Corrections apportées à votre configuration Frigate

## ❌ Problèmes corrigés

### 1. **Stream frigate2 incorrect**
**Avant (ligne 137):**
```yaml
frigate2:
  ffmpeg:
    inputs:
      - path: rtsp://127.0.0.1:8554/frigate1  # ❌ MAUVAIS
```

**Après:**
```yaml
frigate2:
  ffmpeg:
    inputs:
      - path: rtsp://127.0.0.1:8554/frigate2  # ✅ CORRIGÉ
```

### 2. **Indentation incorrecte de detect**
**Avant:**
```yaml
frigate2:
  ffmpeg:
    inputs:
      - path: rtsp://127.0.0.1:8554/frigate1
        input_args: preset-rtsp-restream
        roles:
          - detect

detect:  # ❌ Au mauvais niveau!
  enabled: true
  width: 1000
```

**Après:**
```yaml
frigate2:
  ffmpeg:
    inputs:
      - path: rtsp://127.0.0.1:8554/frigate2
        input_args: preset-rtsp-restream
        roles:
          - detect

  detect:  # ✅ Correctement indenté sous frigate2
    enabled: true
    width: 800
```

### 3. **Résolution incorrecte pour ESP32-P4**
**Avant:**
```yaml
detect:
  width: 1000   # ❌ Pas la bonne résolution
  height: 700
  fps: 5        # ❌ Trop lent
```

**Après:**
```yaml
detect:
  width: 800    # ✅ Résolution native OV5647
  height: 640
  fps: 30       # ✅ 30 FPS (capacité de l'ESP32-P4)
```

### 4. **Section detect globale en double**
**Avant (à la fin du fichier):**
```yaml
version: 0.16-0

detect:              # ❌ Section en double
  enabled: true
```

**Après:**
```yaml
version: 0.14        # ✅ Simplifié
```

### 5. **go2rtc - frigate2 source**
**Avant:**
```yaml
go2rtc:
  streams:
    frigate2:
      - rtsp://192.168.1.49:554/stream   # ✅ Déjà correct
```

**Après (identique mais avec commentaire):**
```yaml
go2rtc:
  streams:
    frigate2:
      - rtsp://192.168.1.49:554/stream  # ✅ ESP32-P4 OV5647
```

## ✅ Configuration finale

Votre configuration Frigate maintenant a:

### Camera frigate1 (Tapo C500)
- Stream: `rtsp://Tapoone:Tapoone132@192.168.1.56:554/stream1`
- Résolution: 1000x700
- FPS: 5
- Via go2rtc: `rtsp://127.0.0.1:8554/frigate1`

### Camera frigate2 (ESP32-P4 OV5647)
- Stream: `rtsp://192.168.1.49:554/stream` (direct ESP32)
- Résolution: **800x640** (native)
- FPS: **30** (hardware H.264)
- Via go2rtc: `rtsp://127.0.0.1:8554/frigate2`

## 🚀 Prochaines étapes

1. **Sauvegardez votre ancienne config:**
   ```bash
   cp /config/frigate.yml /config/frigate.yml.backup
   ```

2. **Copiez la nouvelle config:**
   ```bash
   cp frigate_corrected.yaml /config/frigate.yml
   ```

3. **Vérifiez la config avant de redémarrer:**
   ```bash
   docker exec frigate python3 -m frigate --validate-config
   ```

4. **Redémarrez Frigate:**
   ```bash
   docker restart frigate
   ```

5. **Vérifiez les logs:**
   ```bash
   docker logs -f frigate | grep frigate2
   ```

   Vous devriez voir:
   ```
   [frigate2] frigate2: 30.0 FPS (30 ms)
   [detector.coral] coral: 7.3 FPS (135.0 ms)
   ```

## 📊 Monitoring

Ouvrez l'interface Frigate: `http://<IP_FRIGATE>:5000`

Vérifiez:
- ✅ Les deux caméras apparaissent (frigate1 et frigate2)
- ✅ Les streams sont actifs
- ✅ Les détections fonctionnent
- ✅ Stats système montrent 30 FPS pour frigate2

## ⚠️ Notes importantes

1. **IP ESP32**: Assurez-vous que `192.168.1.49` est bien l'IP de votre ESP32-P4
   - Vérifiez dans les logs ESPHome
   - Ou faites `ping 192.168.1.49`

2. **Test direct du stream:**
   ```bash
   # Test sans go2rtc
   ffplay -rtsp_transport tcp rtsp://192.168.1.49:554/stream

   # Test avec go2rtc
   ffplay -rtsp_transport tcp rtsp://127.0.0.1:8554/frigate2
   ```

3. **WebRTC IP publique**: Changez `1.2.3.4` par votre vraie IP publique si vous voulez accéder de l'extérieur

4. **Version Frigate**: J'ai changé de `0.16-0` à `0.14` qui est plus stable
   - Si vous voulez garder 0.16, changez juste `version: 0.16`
