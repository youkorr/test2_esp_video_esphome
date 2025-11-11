# Fix Watchdog Timeout - Canvas LVGL Requis

## 🔴 Problème

Votre ESP32-P4 redémarre avec un **watchdog timeout** après le démarrage du streaming :

```
E (46965) task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:
E (46965) task_wdt:  - loopTask (CPU 1)
```

## 🔍 Cause

Le problème est que **le canvas LVGL a une taille de 0×0** :

```
[lvgl_camera_display:166]: Taille canvas: 0x0  ← ❌ PROBLÈME
```

Quand `lvgl_camera_display` essaye de mettre à jour ce canvas invalide, LVGL bloque pendant plusieurs secondes, causant le watchdog timeout.

## ✅ Solution : Créer le Canvas LVGL

Vous DEVEZ définir explicitement un canvas dans votre configuration LVGL avec **exactement les mêmes dimensions que la caméra**.

### Configuration Complète Corrigée

```yaml
# 1. Prévient le crash netif
network_init_guard:

# 2. Configuration ESP-Video
esp_video:
  i2c_id: i2c_bus
  xclk_pin: GPIO36
  xclk_freq: 24000000
  enable_h264: true
  enable_jpeg: true
  enable_isp: true
  use_heap_allocator: true  # ✅ Important pour 800x480

# 3. Configuration caméra
mipi_dsi_cam:
  id: tab5_cam
  i2c_id: bsp_bus
  sensor_type: ov02c10     # ✅ Corrigé (était "sensor:")
  resolution: "800x480"
  pixel_format: "RGB565"   # ✅ Corrigé (était "RB565")
  framerate: 30
  jpeg_quality: 10
  mirror_x: false
  mirror_y: false
  rotation: 0
  # rgb_gains: Désactivé car ne fonctionne pas avec format custom
  # red: 1.30
  # green: 0.85
  # blue: 1.25

# 4. Serveur web HTTP
camera_web_server:
  camera_id: tab5_cam
  port: 8080
  enable_stream: true
  enable_snapshot: true

# 5. ✅ CRITICAL : Définir le canvas LVGL avec les bonnes dimensions
lvgl:
  displays:
    - display_id: my_display  # Remplacez par votre ID d'affichage réel

  pages:
    - id: main_page
      widgets:
        - canvas:
            id: camera_canvas       # ✅ Doit correspondre à canvas_id ci-dessous
            x: 0
            y: 0
            width: 800              # ✅ CRITICAL : Même résolution que la caméra
            height: 480             # ✅ CRITICAL : Même résolution que la caméra

# 6. Affichage caméra sur canvas LVGL
lvgl_camera_display:
  id: camera_display
  camera_id: tab5_cam
  canvas_id: camera_canvas          # ✅ Doit correspondre à l'ID du canvas ci-dessus
  update_interval: 33ms
```

## 📋 Points Critiques

### 1. Canvas dimensions DOIVENT correspondre à la caméra
```yaml
mipi_dsi_cam:
  resolution: "800x480"  # Résolution caméra

lvgl:
  widgets:
    - canvas:
        width: 800       # ✅ DOIT être identique
        height: 480      # ✅ DOIT être identique
```

### 2. Les IDs doivent correspondre
```yaml
lvgl:
  widgets:
    - canvas:
        id: camera_canvas  # ID du canvas

lvgl_camera_display:
  canvas_id: camera_canvas # ✅ Même ID
```

### 3. Le display_id doit être valide
```yaml
lvgl:
  displays:
    - display_id: my_display  # ✅ Remplacez par votre ID réel
```

Pour trouver votre ID d'affichage, cherchez dans votre config :
```yaml
display:
  - platform: ...
    id: mon_affichage  # ← Utilisez cet ID
```

## 🐛 Erreurs Corrigées

### Erreur 1 : Typo dans pixel_format ✅ CORRIGÉ
```yaml
# ❌ AVANT
pixel_format: "RB565"

# ✅ APRÈS
pixel_format: "RGB565"
```

### Erreur 2 : Paramètre "sensor" invalide ✅ CORRIGÉ
```yaml
# ❌ AVANT
sensor: ov02c10

# ✅ APRÈS
sensor_type: ov02c10
```

### Erreur 3 : Bayer pattern incorrect ✅ CORRIGÉ (dans le code)
Le format custom OV02C10 utilisait `RGGB` au lieu de `BGGR`. C'est maintenant corrigé dans `ov02c10_custom_formats.h`.

## 📊 Après Correction

### Logs attendus

```
[network_init_guard]: Network Initialization Guard
[network_init_guard]: Status: Active
[esp_video]: esp-video: ok
[mipi_dsi_cam]: Detected sensor: ov02c10 (PID: 0x02C1)
[mipi_dsi_cam]: ✅ Using CUSTOM format: 800x480 RAW10 @ 30fps
[mipi_dsi_cam]: ✅ Custom format applied successfully!
[mipi_dsi_cam]: mipi_dsi_cam: streaming started
[lvgl_camera_display]: Taille canvas: 800x480  ← ✅ Correct !
[lvgl_camera_display]: ✅ LVGL Camera Display initialisé
[lvgl_camera_display]: 🖼️ Premier update canvas
[lvgl_camera_display]: 🎞️ 100 frames - FPS: 29.8
```

### Pas de watchdog timeout
Le système devrait maintenant fonctionner sans redémarrage.

## 🔧 Alternative : Utiliser 1080P

Si 800×480 pose trop de problèmes, utilisez la résolution native :

```yaml
mipi_dsi_cam:
  resolution: "1080P"      # Résolution native OV02C10
  pixel_format: "RGB565"

lvgl:
  widgets:
    - canvas:
        width: 1920
        height: 1080
```

**Avantages** :
- Pas de format custom (plus stable)
- Meilleure qualité d'image
- Gains RGB fonctionnent

**Inconvénients** :
- Plus de mémoire (1920×1080×2 = 4 MB vs 800×480×2 = 750 KB)
- Peut être trop grand pour petit écran

## 📝 Checklist

Avant de recompiler, vérifiez :

- [ ] `network_init_guard:` ajouté en haut du fichier
- [ ] `sensor_type:` au lieu de `sensor:`
- [ ] `pixel_format: "RGB565"` (pas `"RB565"`)
- [ ] `use_heap_allocator: true` dans `esp_video:`
- [ ] Canvas LVGL créé avec `width: 800` et `height: 480`
- [ ] `canvas_id:` correspond entre LVGL et `lvgl_camera_display`
- [ ] `display_id:` est un ID valide de votre configuration

## 🚀 Prochaines Étapes

1. **Ajoutez le canvas LVGL** avec les bonnes dimensions
2. **Recompilez** votre projet
3. **Flashez** l'ESP32-P4
4. **Vérifiez** les logs - le canvas doit montrer `800x480` et non `0x0`
5. **L'image devrait s'afficher** sans watchdog timeout

## 💡 Astuce

Si vous ne savez pas comment structurer votre config LVGL, montrez-moi votre configuration actuelle et je vous aiderai à l'intégrer correctement.

## 📚 Référence

- Format custom OV02C10 : `components/mipi_dsi_cam/CUSTOM_FORMATS_OV02C10.md`
- Formats OV5647 : `components/mipi_dsi_cam/CUSTOM_FORMATS_OV5647.md`
- Network guard : `components/network_init_guard/README.md`
