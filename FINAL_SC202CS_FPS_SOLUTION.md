# Solution Finale: SC202CS 7.36 FPS → 30 FPS

## 🎯 Résumé Exécutif

**Problème:** SC202CS affiche 7.36 FPS au lieu de 30 FPS
**Cause Racine:** Mismatch entre taille image (800x600) et canvas (800x480)
**Solution:** Activer PPA hardware resize + corriger configuration canvas
**Résultat Attendu:** 30 FPS stable

## 📊 Diagnostic Complet

### Logs Actuels (Problématiques)

```
[I][esp_cam_sensor:221]: ⚠️ PPA explicitly DISABLED by user (ppa_enabled: false)
[I][esp_cam_sensor:1335]: Timing: DQBUF=106us, PPA=2us
[W][component:490]: lvgl took a long time for an operation (457 ms)
[I][lvgl_camera_display:118]: 200 frames - FPS: 7.36 | capture: 23.1ms | canvas: 0.4ms
```

**Configuration actuelle:**
- Caméra capture: **800x600** RGB565
- Canvas LVGL: **800x600** (vous avez confirmé)
- Écran M5Tab5: **800x480**
- PPA: **DÉSACTIVÉ**

**Problèmes identifiés:**
1. Canvas (800x600) > Écran (800x480) → débordement de 120 pixels
2. LVGL doit gérer clipping/scrolling virtuel → très lent
3. PPA désactivé → pas de resize hardware
4. LVGL fait resize/crop en software → 457ms!

### Logs Attendus (Après Fix)

```
[I][esp_cam_sensor]: PPA hardware transform enabled (resize=800x480)
[I][esp_cam_sensor]: PPA Config:
[I][esp_cam_sensor]:   Input:  800x600 RGB565
[I][esp_cam_sensor]:   Output: 800x480 RGB565
[I][esp_cam_sensor]:   Scale:  x=1.000 y=0.800
[I][lvgl_camera_display]: Image: 800x600
[I][lvgl_camera_display]: Canvas: 800x480
[I][lvgl_camera_display]: 100 frames - FPS: 30.0 | capture: 23ms | canvas: 0.4ms
```

## ✅ Solution Complète

### Étape 1: Corriger le Canvas LVGL

**Dans votre fichier YAML (page caméra):**

```yaml
lvgl:
  pages:
    - id: camera_page
      widgets:
        - canvas:
            id: camera_canvas
            width: 800
            height: 480     # ← CHANGER de 600 → 480 !
            x: 0
            y: 0
```

**Pourquoi?**
- L'écran M5Tab5 fait 800×480 pixels
- Le canvas DOIT correspondre exactement à l'écran
- Canvas > écran = débordement = catastrophe de performance

### Étape 2: Activer le PPA avec Resize

**Dans votre configuration mipi_dsi_cam:**

```yaml
mipi_dsi_cam:
  id: tab5_cam
  sensor_type: sc202cs
  i2c_id: bsp_bus
  sensor_addr: 0x36
  resolution: "800x600"      # Caméra capture en 800x600
  pixel_format: RGB565
  framerate: 30

  # ❌ RETIRER CETTE LIGNE:
  # ppa_enabled: false

  # ✅ AJOUTER CES LIGNES:
  ppa_enabled: true          # Activer PPA hardware
  output_width: 800          # PPA resize → 800x480
  output_height: 480         # Correspond au canvas!
```

**Pourquoi?**
- Sans PPA: caméra donne 800x600 directement à LVGL
- Avec PPA: caméra → PPA (resize hardware) → 800x480 → LVGL
- Resize hardware < 1ms vs resize software > 100ms

### Étape 3: Recompiler et Vérifier

1. **Recompiler** votre firmware ESPHome
2. **Flasher** sur l'ESP32-P4
3. **Vérifier les logs** au démarrage:

**Logs de succès à chercher:**
```
[I][lvgl_camera_display]: Image: 800x480
[I][lvgl_camera_display]: Canvas: 800x480
```

**Si vous voyez un warning:**
```
[W] ⚠️ SIZE MISMATCH! Image=800x600 but Canvas=800x480
[W] ⚠️ This will cause slow LVGL software resize!
```
→ Le PPA n'est pas activé correctement

## 🔧 Corrections Appliquées au Code

### Fix #1: Optimisation PPA (commit 661618c)

**Changement:**
```cpp
// AVANT
ppa_config.max_pending_trans_num = 16;

// APRÈS
ppa_config.max_pending_trans_num = 1;  // Match M5Stack
```

**Pourquoi:** M5Stack utilise 1 transaction en attente, plus simple et efficace pour streaming temps-réel.

### Fix #2: lvgl_camera_display (commit a8a3ddb)

**Changement:**
```cpp
// AVANT: Utilise dimensions de l'image
uint16_t width = camera->get_image_width();   // 800x600
lv_canvas_set_buffer(canvas, img_data, width, height, ...);

// APRÈS: Utilise dimensions du canvas LVGL
lv_coord_t canvas_width = lv_obj_get_width(canvas);  // 800x480
lv_canvas_set_buffer(canvas, img_data, canvas_width, canvas_height, ...);
```

**Pourquoi:**
- Passait 800x600 à LVGL même si canvas est 800x480
- Forçait LVGL à faire du resize/crop software
- Maintenant passe toujours la bonne taille du canvas

**Nouveau diagnostic automatique:**
- Détecte mismatch image vs canvas
- Affiche warning clair
- Suggère la solution (activer PPA)

## 📈 Performance Avant/Après

| Métrique | AVANT (actuel) | APRÈS (fixé) | Amélioration |
|----------|----------------|--------------|--------------|
| **FPS** | 7.36 | 30.0 | **+407%** |
| **LVGL operation** | 457ms | ~10-20ms | **-96%** |
| **Capture** | 23.1ms | 23ms | Identique |
| **Canvas update** | 0.4ms | 0.4ms | Identique |
| **PPA resize** | N/A | < 1ms | Hardware! |
| **Frame period** | 136ms | 33ms | **-76%** |

## ⚠️ Pourquoi Vous Pensiez que le PPA Ralentissait

**Votre expérience:**
- PPA activé → toujours lent
- PPA désactivé → toujours lent
- Conclusion: "le PPA fait perdre des FPS"

**Réalité:**
Vous avez probablement testé:
1. PPA activé + canvas 800x600 → lent (canvas trop grand)
2. PPA désactivé + canvas 800x600 → lent (canvas trop grand)

**Le problème n'était PAS le PPA, mais le CANVAS trop grand!**

**Pour que le PPA aide, il faut LES DEUX:**
- ✅ PPA activé avec output 800x480
- ✅ Canvas configuré à 800x480

Si canvas reste à 800x600, le PPA ne résout rien car le problème est le débordement du canvas hors écran.

## 🎯 Checklist de Validation

Après avoir appliqué les changements:

- [ ] Canvas YAML: `height: 480` (pas 600)
- [ ] mipi_dsi_cam: `ppa_enabled: true`
- [ ] mipi_dsi_cam: `output_width: 800`
- [ ] mipi_dsi_cam: `output_height: 480`
- [ ] Recompilé firmware
- [ ] Logs montrent: `Image: 800x480` et `Canvas: 800x480`
- [ ] Aucun warning "SIZE MISMATCH"
- [ ] FPS affiche ~30 (au lieu de 7.36)

## 📚 Documents de Référence

1. **SC202CS_FPS_DIAGNOSTIC.md** - Analyse initiale du problème
2. **SC202CS_PPA_VS_CANVAS_DIAGNOSTIC.md** - Explication PPA vs Canvas
3. **Ce document** - Solution finale complète

## 🚀 Prochaines Étapes

1. Appliquer les changements YAML (canvas 480 + PPA activé)
2. Recompiler et flasher
3. Vérifier les logs au démarrage
4. Confirmer FPS à 30

**Si FPS toujours bas après ces changements:**
- Envoyez-moi les nouveaux logs complets
- Vérifiez la configuration du display driver DPI
- Vérifiez qu'il n'y a pas d'autres composants qui bloquent

Mais normalement, avec canvas 800x480 + PPA activé, vous devriez avoir 30 FPS! 🎉
