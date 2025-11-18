# Guide d'installation Frigate pour ESP32-P4 Camera (frigate2)

## 📋 Configuration créée

- **Nom de la caméra**: `frigate2`
- **Résolution**: 800x640 @ 30fps
- **Codec**: H.264 (hardware)
- **Stream**: `rtsp://<IP_ESP32>:554/stream`

## 🚀 Installation rapide

### 1. Trouvez l'IP de votre ESP32-P4

Dans les logs ESPHome après le démarrage, ou sur votre routeur.

```
[I][wifi:xxx]: WiFi Connected
[I][wifi:xxx]: IP Address: 192.168.1.XXX
```

### 2. Testez le stream RTSP

Avant d'intégrer dans Frigate, testez que le stream fonctionne:

```bash
# Avec VLC
vlc rtsp://192.168.1.XXX:554/stream

# Avec FFplay
ffplay -rtsp_transport tcp rtsp://192.168.1.XXX:554/stream

# Avec FFmpeg (test de lecture)
ffmpeg -rtsp_transport tcp -i rtsp://192.168.1.XXX:554/stream -f null -
```

### 3. Intégrez dans Frigate

#### Option A: Nouvelle installation Frigate

Copiez le fichier `frigate_config.yaml` vers `/config/frigate.yml`:

```bash
cp frigate_config.yaml /config/frigate.yml
```

Puis éditez et changez:
- `192.168.1.XXX` → L'IP de votre ESP32
- `192.168.1.100` → L'IP de votre broker MQTT (si utilisé)

#### Option B: Frigate existant

Ajoutez uniquement la section caméra dans votre `/config/frigate.yml` existant:

```yaml
cameras:
  frigate2:  # ← Nouvelle caméra
    enabled: true
    ffmpeg:
      inputs:
        - path: rtsp://192.168.1.XXX:554/stream  # ← Votre IP ESP32
          roles:
            - detect
            - record
      hwaccel_args: preset-vaapi  # Adaptez selon votre hardware
      input_args: preset-rtsp-generic
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
        - car
    record:
      enabled: true
      retain:
        days: 7
        mode: motion
    snapshots:
      enabled: true
```

### 4. Redémarrez Frigate

```bash
# Docker
docker restart frigate

# Docker Compose
docker-compose restart frigate

# Home Assistant addon
# Allez dans Supervisor → Frigate → Restart
```

### 5. Vérifiez dans l'interface Frigate

Ouvrez l'interface web de Frigate: `http://<IP_FRIGATE>:5000`

Vous devriez voir:
- La caméra `frigate2` dans la liste
- Le stream vidéo en direct
- Les détections d'objets (personnes, animaux, etc.)

## 🔧 Optimisations recommandées

### Accélération matérielle

Choisissez selon votre serveur Frigate:

```yaml
# Intel CPU avec iGPU (Celeron, i3, i5, i7)
hwaccel_args: preset-vaapi

# Raspberry Pi 4 ou 5
hwaccel_args: preset-rpi-64-h264

# NVIDIA GPU
hwaccel_args: preset-nvidia-h264

# AMD GPU
hwaccel_args: preset-amd64-vaapi

# Pas d'accélération (CPU uniquement)
hwaccel_args: []
```

### Zones de détection

Définissez des zones spécifiques pour éviter les fausses détections:

```yaml
cameras:
  frigate2:
    detect:
      zones:
        zone_entree:
          coordinates: 0,0,800,300,800,640,0,640
          objects:
            - person
        zone_parking:
          coordinates: 0,300,800,300,800,640,0,640
          objects:
            - car
            - bicycle
```

### Masques de mouvement

Ignorez les zones avec mouvement constant (arbres, drapeaux):

```yaml
cameras:
  frigate2:
    motion:
      mask:
        - 0,0,200,0,200,100,0,100  # Zone haut-gauche ignorée
```

## 📊 Monitoring

### Logs Frigate

```bash
# Docker
docker logs -f frigate

# Home Assistant
# Supervisor → Frigate → Logs
```

Recherchez:
```
[frigate2] frigate2: 30.0 FPS (30 ms)
[detector.coral] coral: 7.3 FPS (135.0 ms)
```

### Statistiques

Dans l'interface Frigate → System → Stats:
- **FPS caméra**: Devrait être ~30 FPS
- **FPS détection**: Variable selon le CPU/Coral
- **Latence**: < 200ms recommandé

## 🏠 Intégration Home Assistant

Si vous utilisez l'addon Frigate dans Home Assistant:

1. La caméra apparaît automatiquement comme:
   - `camera.frigate2`
   - Sensors de détection: `binary_sensor.frigate2_person_occupancy`

2. Créez une carte Lovelace:

```yaml
type: picture-glance
title: Caméra ESP32 (frigate2)
camera_image: camera.frigate2
entities:
  - binary_sensor.frigate2_person_occupancy
  - binary_sensor.frigate2_motion
  - sensor.frigate2_detection_fps
```

## 🐛 Dépannage

### Caméra offline dans Frigate

```bash
# Testez manuellement
ffmpeg -rtsp_transport tcp -i rtsp://192.168.1.XXX:554/stream -f null -

# Vérifiez les logs ESP32
# Dans ESPHome, vous devriez voir:
# [I][rtsp_server:452]: Initializing H.264 encoder (first client)...
# [I][rtsp_server:473]: Session XXXXXXXX started playing
```

### Détections manquantes

1. **Baissez le threshold**:
   ```yaml
   filters:
     person:
       threshold: 0.6  # Au lieu de 0.75
   ```

2. **Vérifiez l'éclairage**: Le détecteur fonctionne mieux en plein jour

3. **Augmentez la sensibilité du mouvement**:
   ```yaml
   motion:
     threshold: 20  # Au lieu de 30
   ```

### Performance faible

1. **Utilisez hwaccel** (Intel VAAPI, RPi, NVIDIA)
2. **Réduisez le nombre d'objets trackés**
3. **Limitez les zones de détection**

## 📚 Resources

- [Frigate Documentation](https://docs.frigate.video/)
- [Frigate Configuration Reference](https://docs.frigate.video/configuration/)
- [go2rtc Documentation](https://github.com/AlexxIT/go2rtc)
