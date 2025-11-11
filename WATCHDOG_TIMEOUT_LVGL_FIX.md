# Fix Watchdog Timeout avec LVGL Camera Display

**Problème:** ESP32-P4 redémarre après 5 secondes avec watchdog timeout quand le streaming démarre.

**Date:** 2025-11-10
**Sensor:** OV02C10 800×480
**ESP-IDF:** 5.5.1

## 🔍 Symptômes

```
[00:45:22] streaming started
[00:45:22][W] lvgl took a long time for an operation (235 ms)  ← LVGL bloque!
[00:45:22][W] Components should block for at most 30 ms
[00:45:27] Task watchdog got triggered                        ← Reboot après 5s
```

## ❌ Cause Racine

Le composant `lvgl_camera_display` avec un `update_interval` de 33ms (30 FPS) essaie de capturer et dessiner des frames sur le canvas LVGL trop rapidement au démarrage du streaming. LVGL bloque pendant 235ms, empêchant la task `loop()` de répondre au watchdog.

**Pourquoi ça bloque:**
1. Le streaming vient de démarrer mais les buffers V4L2 ne sont pas encore prêts
2. `lvgl_camera_display` appelle `capture_frame()` immédiatement
3. La capture attend des données valides, bloquant LVGL
4. Après 5 secondes sans réponse de la task loop, le watchdog force un reboot

## ✅ Solution 1: Augmenter l'Update Interval (Recommandé)

Réduisez la fréquence de refresh pour donner plus de temps entre les captures:

```yaml
lvgl_camera_display:
  id: camera_display
  camera_id: tab5_cam
  canvas_id: camera_canvas
  update_interval: 100ms  # 10 FPS au lieu de 30 FPS
```

**Avantages:**
- ✅ Moins de charge CPU
- ✅ Plus de temps pour la task loop
- ✅ Évite les blocages au démarrage
- ✅ Toujours fluide pour un affichage sur LCD

**Inconvénient:**
- ⚠️  Framerate réduit à 10 FPS sur l'écran (mais streaming HTTP reste 30 FPS)

## ✅ Solution 2: Augmenter le Timeout Watchdog

Si vous voulez garder 30 FPS sur l'écran, augmentez le timeout du watchdog:

```yaml
esphome:
  name: p4mini
  platformio_options:
    board_build.extra_flags:
      - -DCONFIG_ESP_TASK_WDT_TIMEOUT_S=10  # 10 secondes au lieu de 5
```

**Avantages:**
- ✅ Garde 30 FPS sur l'écran
- ✅ Plus de temps pour les opérations longues au démarrage

**Inconvénient:**
- ⚠️  Le watchdog mettra plus de temps à détecter les vrais freezes

## ✅ Solution 3: Désactiver lvgl_camera_display Temporairement

Pour tester si le streaming fonctionne sans l'affichage LVGL:

```yaml
# Commentez:
# lvgl_camera_display:
#   id: camera_display
#   camera_id: tab5_cam
#   canvas_id: camera_canvas
#   update_interval: 33ms
```

Puis testez le streaming HTTP:
- URL: `http://<ip>:8080/stream`

**Si ça marche:**
- Le problème est dans `lvgl_camera_display`
- Réactivez-le avec `update_interval: 100ms`

**Si ça ne marche pas:**
- Le problème est ailleurs (driver caméra, ISP, etc.)

## ✅ Solution 4: Ajouter un Délai au Démarrage

Ajoutez un délai avant que `lvgl_camera_display` commence à capturer:

```yaml
lvgl:
  pages:
    - id: camera_page
      on_load:
        - lambda: |-
            ESP_LOGI("camera", "📸 Page caméra chargée");

        # Attendre 500ms pour que le streaming se stabilise
        - delay: 500ms

        - lambda: |-
            // Maintenant démarrer le streaming
            if (id(tab5_cam).start_streaming()) {
              ESP_LOGI("camera", "✅ Streaming démarré");
            }
```

## ✅ Solution 5: Utiliser Camera Web Server Sans LVGL Display

Si vous voulez juste streamer sans afficher sur l'écran LCD:

```yaml
# Gardez camera_web_server
camera_web_server:
  camera_id: tab5_cam
  port: 8080
  enable_stream: true

# Supprimez lvgl_camera_display
# lvgl_camera_display:
#   ...

# Ajoutez un bouton pour démarrer/arrêter le streaming
lvgl:
  pages:
    - id: camera_page
      widgets:
        - button:
            on_click:
              then:
                - lambda: |-
                    if (!id(tab5_cam).is_streaming()) {
                      id(tab5_cam).start_streaming();
                      ESP_LOGI("camera", "Streaming HTTP actif sur port 8080");
                    }
```

Accédez au stream via: `http://<ip>:8080/stream`

## 📊 Comparaison des Solutions

| Solution | FPS Écran | FPS Stream HTTP | Charge CPU | Risque Watchdog |
|----------|-----------|-----------------|------------|-----------------|
| **update_interval: 33ms** | 30 | 30 | Élevée | ❌ Timeout |
| **update_interval: 100ms** | 10 | 30 | Moyenne | ✅ OK |
| **update_interval: 200ms** | 5 | 30 | Faible | ✅ OK |
| **Watchdog 10s** | 30 | 30 | Élevée | ✅ OK (avec délai) |
| **Sans lvgl_camera_display** | 0 | 30 | Faible | ✅ OK |

## 🎯 Recommandation Finale

**Pour un usage normal avec affichage LVGL:**

```yaml
lvgl_camera_display:
  id: camera_display
  camera_id: tab5_cam
  canvas_id: camera_canvas
  update_interval: 100ms  # 10 FPS - fluide et stable
```

**Pour une performance maximale sans affichage:**

Supprimez `lvgl_camera_display` et utilisez uniquement `camera_web_server` sur port 8080.

## 🔧 Configuration Complète Recommandée

```yaml
# Caméra
mipi_dsi_cam:
  id: tab5_cam
  i2c_id: bsp_bus
  sensor_type: ov02c10
  resolution: "800x480"
  pixel_format: RGB565
  framerate: 30

# Streaming HTTP (toujours 30 FPS)
camera_web_server:
  camera_id: tab5_cam
  port: 8080
  enable_stream: true

# Affichage LVGL (10 FPS pour éviter watchdog)
lvgl_camera_display:
  id: camera_display
  camera_id: tab5_cam
  canvas_id: camera_canvas
  update_interval: 100ms  # ← CLEF: 100ms au lieu de 33ms

# Page LVGL
lvgl:
  pages:
    - id: camera_page
      widgets:
        - canvas:
            id: camera_canvas
            width: 800
            height: 480

        - button:
            text: "START"
            on_click:
              - lambda: id(tab5_cam).start_streaming();

        - button:
            text: "STOP"
            on_click:
              - lambda: id(tab5_cam).stop_streaming();
```

## 📝 Logs Attendus Après Fix

```
[00:00:10] streaming started
[00:00:10] lvgl camera display: updating canvas  ← Pas de blocage!
[00:00:10] Frame captured: 800x480
[00:00:11] Frame captured: 800x480
...
```

**Pas de:**
- ❌ `lvgl took a long time for an operation`
- ❌ `Task watchdog got triggered`
- ❌ Reboot après 5 secondes

## 🚨 Si le Problème Persiste

Si même avec `update_interval: 100ms` vous avez toujours le watchdog timeout:

1. **Vérifiez les logs** pour voir où ça bloque exactement
2. **Essayez `update_interval: 200ms`** (5 FPS)
3. **Augmentez le watchdog timeout** à 10 secondes
4. **Vérifiez la mémoire disponible**: `free -h` sur ESP32-P4
5. **Testez sans LVGL display** pour confirmer que le streaming fonctionne

## 📚 Références

- [ESPHome LVGL Component](https://esphome.io/components/lvgl.html)
- [ESP32-P4 Task Watchdog](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/system/wdts.html)
- [V4L2 Streaming](https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/stream.html)

---

**Résumé:** Changez `update_interval: 33ms` → `update_interval: 100ms` dans `lvgl_camera_display` pour éviter le watchdog timeout. L'affichage sera toujours fluide à 10 FPS, et le streaming HTTP restera à 30 FPS.
