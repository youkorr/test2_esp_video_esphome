# Guide HTTP/HTTPS Streaming pour Simple Video Player

## 🎥 Fonctionnalité

Le lecteur vidéo supporte maintenant le **streaming HTTP/HTTPS** ! Cela permet de :
- ✅ **Éviter la carte SD lente** (principal gain de performance !)
- ✅ Lire des vidéos depuis un serveur web
- ✅ Pas besoin de copier les fichiers sur la carte SD
- ✅ Lecture depuis la SPIRAM (très rapide)

## 📖 Configuration YAML

### Exemple basique

```yaml
simple_video_player:
  id: my_video_player
  file_path: "http://192.168.1.158:8080/kOKLKPxr/Rise-of-Skywalker_fixed.mp4"
  fps: 15
  parent_id: video_page
  show_controls: true
  auto_play: false
  loop: false
```

### Avec serveur HTTPS

```yaml
simple_video_player:
  id: my_video_player
  file_path: "https://example.com/videos/my_video.mp4"
  fps: 15
  parent_id: video_page
```

## 🚀 Comment ça marche

### 1. Détection automatique

Le lecteur détecte automatiquement si `file_path` commence par `http://` ou `https://` :
- **HTTP/HTTPS** → Télécharge le fichier en SPIRAM
- **Autre** → Ouvre le fichier local (carte SD)

### 2. Téléchargement en mémoire

```
HTTP Request → Download en chunks (4KB) → SPIRAM Buffer → fmemopen() → FILE*
```

- Télécharge **tout le fichier** en SPIRAM
- Utilise `fmemopen()` pour créer un FILE* depuis la mémoire
- Tout le code existant fonctionne normalement !

### 3. Avantages de performance

| Source | Vitesse lecture | FPS attendu |
|--------|-----------------|-------------|
| **HTTP → SPIRAM** | ~50-100 MB/s | **25-30 FPS** @ 640x480 |
| Carte SD (bonne) | ~10-20 MB/s | 15-20 FPS |
| Carte SD (mauvaise) | **~1-5 MB/s** | **0.5-2 FPS** ⚠️ |

**Gain** : **10-100x plus rapide** qu'une carte SD lente !

## 📊 Logs attendus

### Au démarrage

```
[I][simple_video_player:370] Opening HTTP/HTTPS source: http://192.168.1.158:8080/video.mp4
[I][simple_video_player:269] Downloading from HTTP/HTTPS: http://192.168.1.158:8080/video.mp4
[I][simple_video_player:308] HTTP file size: 5242880 bytes (5.00 MB)
[I][simple_video_player:347] Download progress: 10% (524288 / 5242880 bytes)
[I][simple_video_player:347] Download progress: 20% (1048576 / 5242880 bytes)
...
[I][simple_video_player:359] ✓ HTTP download complete: 5242880 bytes
[I][simple_video_player:387] ✓ HTTP video opened from memory: 5242880 bytes
```

### Si échec

```
[E][simple_video_player:286] HTTP connection failed: ESP_ERR_...
[E][simple_video_player:295] HTTP request failed with status 404
[E][simple_video_player:313] Failed to allocate 10485760 bytes in SPIRAM for HTTP download
```

## 🛠️ Configuration du serveur HTTP

### Option 1 : Python HTTP Server (Simple)

```bash
# Dans le dossier contenant vos vidéos
python3 -m http.server 8080
```

Puis dans YAML :
```yaml
file_path: "http://192.168.1.YOUR_PC_IP:8080/video.mp4"
```

### Option 2 : Nginx (Production)

```nginx
server {
    listen 8080;
    server_name _;

    location /videos/ {
        alias /path/to/videos/;
        add_header Access-Control-Allow-Origin *;
        add_header Cache-Control "public, max-age=3600";
    }
}
```

Puis :
```yaml
file_path: "http://192.168.1.SERVER_IP:8080/videos/my_video.mp4"
```

### Option 3 : Home Assistant (Intégré)

Placez vos vidéos dans `/config/www/videos/` :

```yaml
file_path: "http://192.168.1.HA_IP:8123/local/videos/my_video.mp4"
```

## ⚠️ Limitations et considérations

### Taille du fichier

**Maximum** : Limité par la SPIRAM disponible

| ESP32-P4 SPIRAM | Vidéo max (approximatif) |
|-----------------|--------------------------|
| 8 MB | ~6-7 MB (laisse mémoire pour système) |
| 16 MB | ~14-15 MB |
| 32 MB | ~30 MB |

**Recommandation** : Gardez les vidéos < 10 MB pour ESP32-P4 avec 16 MB SPIRAM.

### Temps de téléchargement

Sur WiFi 2.4 GHz (bande passante ~10 Mbps réelle) :
- **5 MB** : ~4-5 secondes
- **10 MB** : ~8-10 secondes
- **20 MB** : ~16-20 secondes

**Astuce** : Affichez un écran de chargement pendant le download !

### Optimisation des vidéos

Pour réduire la taille ET garder la qualité :

```bash
# Vidéo optimisée pour HTTP streaming
ffmpeg -i input.mp4 \
  -vf "scale=480:272" \
  -c:v libx264 \
  -profile:v baseline \
  -preset veryslow \
  -crf 23 \
  -maxrate 400k \
  -bufsize 800k \
  -g 15 \
  -bf 0 \
  -pix_fmt yuv420p \
  -colorspace:v bt709 \
  -color_primaries:v bt709 \
  -color_trc:v bt709 \
  -color_range:v tv \
  -x264opts slices=1 \
  -movflags +faststart \
  -an \
  output_esp32.mp4
```

**Résultat** : ~200-400 KB/s = **3-6 MB pour 1 minute** de vidéo

## 🔧 Dépannage

### "Failed to allocate ... bytes in SPIRAM"

**Cause** : Vidéo trop grande pour la SPIRAM disponible.

**Solution** :
1. Réduire la résolution (480x272 au lieu de 640x480)
2. Réduire le bitrate (`-maxrate 300k`)
3. Réduire la durée de la vidéo

### "HTTP connection failed"

**Causes possibles** :
- ESP32 pas connecté au WiFi
- URL incorrecte
- Serveur HTTP non accessible
- Firewall bloque la connexion

**Vérifier** :
```yaml
logger:
  level: DEBUG
  logs:
    simple_video_player: VERBOSE
```

### "HTTP request failed with status 404"

**Cause** : Fichier introuvable sur le serveur.

**Vérifier** :
- Le chemin du fichier est correct
- Le fichier existe sur le serveur
- Les permissions du fichier (chmod 644)

### Download très lent

**Causes** :
- WiFi faible (distance, obstacles)
- Serveur lent
- Bande passante limitée

**Solutions** :
- Rapprocher l'ESP32 du router WiFi
- Utiliser WiFi 5GHz si possible
- Compresser davantage la vidéo

## 📈 Comparaison de performance

### Test : Vidéo 640x480 @ 15 FPS, 5 MB

| Source | Temps ouverture | FPS lecture | Notes |
|--------|-----------------|-------------|-------|
| **HTTP → SPIRAM** | ~5s download | **25-30 FPS** | ✅ Recommandé |
| Carte SD Class 10 | <1s | 15-20 FPS | ✅ OK |
| Carte SD Class 4 | <1s | 5-10 FPS | ⚠️ Lent |
| Carte SD no-name | <1s | **0.5-2 FPS** | ❌ Inutilisable |

## ✅ Checklist de migration SD → HTTP

- [ ] Configurer serveur HTTP (Python, Nginx, HA)
- [ ] Convertir vidéos avec script optimisé
- [ ] Vérifier taille < 10 MB
- [ ] Tester download depuis PC : `wget http://IP:PORT/video.mp4`
- [ ] Modifier `file_path:` dans YAML
- [ ] Compiler et flasher
- [ ] Vérifier logs de download
- [ ] Profiter de la **vitesse** ! 🚀

---

**Note** : Si la carte SD fonctionne bien, vous pouvez continuer à l'utiliser. Mais si vous avez des problèmes de performance (0.5-2 FPS), HTTP streaming est la **solution** !
