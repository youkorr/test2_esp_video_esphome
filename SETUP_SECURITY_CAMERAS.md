# Guide d'Installation - Caméras de Sécurité Multi-Display

## ⚠️ Problème Courant: "Component not found: multi_camera_display"

Si vous obtenez cette erreur, suivez ces étapes:

### Étape 1: Forcer le rechargement du repository

ESPHome cache les composants externes. Il faut forcer le rechargement:

```bash
# Dans votre interface ESPHome, cliquez sur "Clean Build Files"
# OU supprimez le cache manuellement:
rm -rf /config/.esphome/external_components/
```

### Étape 2: Configuration `external_components`

Dans votre `waveshare.yaml`, utilisez une de ces configurations:

#### Option A: Charger TOUS les composants (recommandé)

```yaml
external_components:
  - source: github://youkorr/test2_esp_video_esphome@claude/fix-network-camera-01FZZGteBXkwYgznQCMFuouU
    refresh: 1min  # Force le rechargement
```

#### Option B: Charger seulement les composants nécessaires

```yaml
external_components:
  - source: github://youkorr/test2_esp_video_esphome@claude/fix-network-camera-01FZZGteBXkwYgznQCMFuouU
    refresh: 1min
    components:
      - network_camera
      - multi_camera_display
      # ... vos autres composants
```

### Étape 3: Configuration des caméras

```yaml
# Définir les caméras réseau
network_camera:
  - id: security_cam_1
    url: "rtsp://admin:password@192.168.1.56:554/stream1"
    protocol: rtsp
    width: 320
    height: 240
    update_interval: 100ms
    canvas_id: dummy  # Requis mais sera remplacé par multi_camera_display

  - id: security_cam_2
    url: "rtsp://admin:password@192.168.1.57:554/stream1"
    protocol: rtsp
    width: 320
    height: 240
    update_interval: 100ms
    canvas_id: dummy

  # ... jusqu'à 4 caméras

# Affichage multi-caméras
multi_camera_display:
  id: security_display
  canvas_id: security_canvas
  cameras:
    - camera_id: security_cam_1
    - camera_id: security_cam_2
```

### Étape 4: Switches (optionnel)

```yaml
switch:
  - platform: network_camera
    name: "Camera 1"
    camera_id: security_cam_1
```

### Étape 5: Page LVGL

Voir le fichier `security_cameras_example.yaml` pour la configuration LVGL complète.

## 🔍 Debugging

Si le problème persiste:

### 1. Vérifier que le repo est bien chargé

Dans les logs de compilation, cherchez:
```
INFO Updating https://github.com/youkorr/test2_esp_video_esphome@claude/fix-network-camera-01FZZGteBXkwYgznQCMFuouU
```

### 2. Vérifier les composants disponibles

Après le téléchargement, ESPHome devrait lister les composants trouvés. Si `multi_camera_display` n'apparaît pas, c'est qu'il n'a pas été téléchargé.

### 3. Tester en local (alternative)

Si GitHub ne fonctionne pas, testez en local:

```yaml
external_components:
  - source:
      type: local
      path: /chemin/vers/test2_esp_video_esphome/components
    components: [network_camera, multi_camera_display]
```

## 📁 Structure Attendue

Vérifiez que votre repo contient:

```
components/
├── multi_camera_display/
│   ├── __init__.py           (1198 bytes)
│   ├── manifest.json         (239 bytes)
│   ├── multi_camera_display.h    (1576 bytes)
│   └── multi_camera_display.cpp  (7245 bytes)
└── network_camera/
    ├── __init__.py           (1389 bytes)
    ├── manifest.json         (219 bytes)
    ├── network_camera.h
    ├── network_camera.cpp
    ├── switch.py
    ├── network_camera_switch.h
    └── network_camera_switch.cpp
```

## 🆘 Aide Supplémentaire

Si vous avez toujours l'erreur:

1. Partagez votre section `external_components` complète
2. Partagez les logs de compilation complets
3. Vérifiez que vous êtes sur la branche `claude/fix-network-camera-01FZZGteBXkwYgznQCMFuouU`

## ✅ Test de Validation

Commande pour valider que les composants sont présents:

```bash
cd /chemin/vers/repo
ls -la components/multi_camera_display/
ls -la components/network_camera/
```

Tous les fichiers doivent avoir les permissions `-rw-r--r--` (644).
