# Système Multi-Caméras de Sécurité (Style Eufy Smart Display E10)

## 🎯 Fonctionnalités

- **Vue en grille 2x2** : Affichage de 4 caméras réseau simultanément
- **Vue plein écran** : Cliquez sur une vignette pour l'agrandir
- **Switch par caméra** : Activez/désactivez chaque caméra individuellement via Home Assistant
- **Support RTSP/H264** : Streaming vidéo depuis caméras IP
- **Support MJPEG** : Alternative pour caméras sans RTSP

## 📦 Composants Créés

### 1. `network_camera`
Composant pour streamer des caméras réseau (RTSP/MJPEG) vers LVGL.

**Corrections apportées:**
- ✅ **Fixé**: Parsing automatique du SDP pour détecter l'URL de contrôle RTSP correcte
- ✅ **Ajouté**: Switch pour activer/désactiver la caméra
- ✅ **Amélioré**: Buffer SDP augmenté à 4096 octets

**Fichiers:**
- `components/network_camera/network_camera.h`
- `components/network_camera/network_camera.cpp`
- `components/network_camera/__init__.py`
- `components/network_camera/switch.py`
- `components/network_camera/network_camera_switch.h`
- `components/network_camera/network_camera_switch.cpp`

### 2. `multi_camera_display`
Composant pour afficher plusieurs caméras en grille avec mode plein écran.

**Fonctionnalités:**
- Grille 2x2 responsive avec LVGL
- Click sur vignette → plein écran
- Bouton BACK → retour à la grille
- Gestion automatique du canvas par caméra

**Fichiers:**
- `components/multi_camera_display/multi_camera_display.h`
- `components/multi_camera_display/multi_camera_display.cpp`
- `components/multi_camera_display/__init__.py`

## 🚀 Utilisation

### Configuration de base

```yaml
# 1. Définir vos caméras réseau
network_camera:
  - id: security_cam_1
    url: "rtsp://admin:password@192.168.1.56:554/stream1"
    protocol: rtsp
    width: 320   # Résolution thumbnail
    height: 240
    update_interval: 100ms
    canvas_id: security_canvas

  - id: security_cam_2
    url: "rtsp://admin:password@192.168.1.57:554/stream1"
    protocol: rtsp
    width: 320
    height: 240
    update_interval: 100ms
    canvas_id: security_canvas

# 2. Créer l'affichage multi-caméras
multi_camera_display:
  id: security_display
  canvas_id: security_canvas
  cameras:
    - camera_id: security_cam_1
    - camera_id: security_cam_2
    - camera_id: security_cam_3
    - camera_id: security_cam_4

# 3. Ajouter des switches (optionnel)
switch:
  - platform: network_camera
    name: "Security Camera 1"
    camera_id: security_cam_1
```

### Configuration LVGL

```yaml
lvgl:
  pages:
    - id: security_page
      on_load:
        - lambda: |-
            auto canvas = id(security_canvas);
            lv_obj_set_size(canvas, 800, 600);
            id(security_display).configure_canvas(canvas);

      widgets:
        - obj:
            id: security_canvas
            width: 800
            height: 520
```

## 🔧 Dépannage

### Erreur de connexion RTSP

**Problème:** `[E][network_camera:064]: Failed to connect to stream`

**Solutions:**
1. Vérifiez l'URL RTSP de votre caméra (utilisez VLC pour tester)
2. Vérifiez les credentials (username:password)
3. Vérifiez que la caméra accepte TCP interleaved mode
4. Regardez les logs pour voir l'URL de contrôle SDP détectée

**Logs utiles:**
```
[I][network_camera:567]: Using control URL from SDP: rtsp://192.168.1.56:554/trackID=0
[I][network_camera:525]: TCP connection established
[I][network_camera:636]: RTSP OPTIONS OK
[I][network_camera:636]: RTSP DESCRIBE OK
[I][network_camera:636]: RTSP SETUP OK
```

### Format d'URL RTSP

Formats supportés:
- `rtsp://192.168.1.56:554/stream1`
- `rtsp://admin:password@192.168.1.56:554/stream1`
- `rtsp://192.168.1.56/live/ch0`

### Résolutions recommandées

**Vue grille (4 caméras):**
- 320x240 ou 400x300 par caméra
- Total canvas: 800x600

**Vue plein écran:**
- 640x480 ou 800x600
- Le composant redimensionne automatiquement

## 📋 Exemple Complet

Voir le fichier `security_cameras_example.yaml` pour une configuration complète avec:
- 4 caméras RTSP
- Grille 2x2 + plein écran
- Boutons START/STOP pour toutes les caméras
- Switches individuels par caméra
- Retour à la page d'accueil

## 🎨 Style Eufy Smart Display E10

Le design s'inspire du Eufy Smart Display E10:
- **Grille 2x2** : 4 vignettes cliquables
- **Plein écran** : Click sur vignette pour agrandir
- **Overlay labels** : Nom de caméra sur chaque vignette
- **Bouton BACK** : Retour rapide à la grille
- **Fond noir** : Design minimaliste

## 🔐 Sécurité

- Les credentials sont encodés en Base64 pour l'authentification RTSP Basic
- Les switches sauvegardent l'état dans RTC (persistant après reboot)
- Les caméras sont désactivées par défaut (économie bande passante)

## ⚡ Performance

**Optimisations:**
- Double buffering pour éviter le flickering
- TCP interleaved mode (pas de ports UDP)
- Décodage H264 matériel (ESP32-P4)
- Frame skipping intelligent

**Consommation réseau:**
- 4 caméras 320x240 @10fps : ~2-4 Mbps
- Mode plein écran 640x480 @15fps : ~1-2 Mbps

## 📞 Support

Si vous rencontrez des problèmes:
1. Activez les logs détaillés: `logger: level: VERBOSE`
2. Vérifiez les messages `[network_camera]` et `[multi_camera_display]`
3. Testez vos URLs RTSP avec VLC avant de configurer ESPHome
