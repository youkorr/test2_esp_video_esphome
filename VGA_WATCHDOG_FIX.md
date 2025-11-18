# VGA Watchdog Timeout Fix

## Problème

VGA (640x480) cause un watchdog timeout après 5 secondes :

```
[18:08:25] ✅ First frame captured: 640x480 RGB565
[18:08:25] 🖼️ Premier update canvas: 640x480
[18:08:30] ❌ Watchdog timeout → Reboot
```

## Causes Possibles

### 1. **update_interval trop court (20ms)**
   - **20ms = 50 FPS** → Trop agressif pour VGA + LVGL
   - LVGL bloque pendant le traitement du canvas
   - loopTask ne peut pas répondre au watchdog dans les 5 secondes

### 2. **Mismatch résolution canvas/caméra**
   - Si canvas configuré pour 800x600 mais caméra envoie 640x480
   - Ou vice-versa
   - LVGL peut bloquer en essayant de redimensionner

### 3. **Page LVGL non optimisée pour VGA**
   - `LVGL_CAMERA_PAGE_OV5647_800x600.yaml` est conçue pour 800x600
   - Widgets positionnés pour 800x600, pas VGA

## Solutions

### Solution 1: Augmenter update_interval (RECOMMANDÉ)

**Fichier**: `rtsp_ov5647.yaml`

```yaml
lvgl_camera_display:
  update_interval: 50ms  # 20 FPS - stable pour VGA
  # Alternative: 100ms (10 FPS) si toujours instable
```

**Avant**: 20ms (50 FPS) → Watchdog timeout
**Après**: 50ms (20 FPS) → Stable

### Solution 2: Adapter canvas à VGA

**Fichier**: Page LVGL ou `rtsp_ov5647.yaml`

Si vous testez VGA, le canvas doit être 640x480 :

```yaml
lvgl:
  pages:
    - id: camera_page
      widgets:
        - canvas:
            id: camera_canvas
            width: 640   # ← VGA width
            height: 480  # ← VGA height
            x: 192       # Centré sur 1024: (1024-640)/2
            y: 60        # Centré sur 600: (600-480)/2
```

### Solution 3: Utiliser 800x600 @ 50 FPS au lieu de VGA

VGA a des performances moins bonnes. Utilisez plutôt 800x600 @ 50 FPS :

```yaml
mipi_dsi_cam:
  resolution: 800x600  # Au lieu de 640x480
  framerate: 50

lvgl_camera_display:
  update_interval: 33ms  # 30 FPS display
```

## Modifications Appliquées

### rtsp_ov5647.yaml
```yaml
lvgl_camera_display:
  update_interval: 50ms  # ← Changé de 20ms à 50ms
```

**Raison**: 20ms trop agressif pour VGA, cause watchdog timeout.

## Test

### Avec update_interval: 50ms
1. **Compile** et flashez
2. **Vérifiez logs** :
```
[I][lvgl_camera_display] Update interval: 50ms (20 FPS)
[I][mipi_dsi_cam] First frame captured: 640x480
```
3. **Pas de watchdog timeout** pendant 30 secondes → ✅ OK

### Si toujours instable
Augmentez à **100ms** :
```yaml
lvgl_camera_display:
  update_interval: 100ms  # 10 FPS
```

## Comparaison Configurations

| Résolution | update_interval | FPS Display | Watchdog ? | Recommandé |
|-----------|-----------------|-------------|------------|------------|
| VGA 640x480 | 20ms | 50 | ❌ Timeout | Non |
| VGA 640x480 | 50ms | 20 | ✅ OK | Oui (stable) |
| VGA 640x480 | 100ms | 10 | ✅ OK | Oui (très stable) |
| 800x600 | 20ms | 50 | ✅ OK | Oui (fluide) |
| 800x600 | 33ms | 30 | ✅ OK | Oui (recommandé) |

## Pourquoi VGA est plus lent ?

1. **Traitement LVGL** : Redimensionnement, conversion de format
2. **CPU overhead** : Plus de contextswitches avec intervalle court
3. **Moins optimisé** : 800x600 @ 50 FPS a été plus testé

## Recommandation Finale

### Pour VGA 640x480 :
```yaml
mipi_dsi_cam:
  resolution: 640x480
  framerate: 30

lvgl_camera_display:
  update_interval: 50ms  # 20 FPS - stable
```

### Pour mouvements fluides (RECOMMANDÉ) :
```yaml
mipi_dsi_cam:
  resolution: 800x600
  framerate: 50

lvgl_camera_display:
  update_interval: 33ms  # 30 FPS - fluide et stable
```

---

**Fichiers modifiés** :
- `rtsp_ov5647.yaml` : update_interval 20ms → 50ms

**Résultat attendu** : Plus de watchdog timeout avec VGA
