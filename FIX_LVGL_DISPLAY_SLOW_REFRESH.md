# Fix LVGL Display Slow Refresh (460ms → 30ms)

## Problème Identifié

Vos logs montrent:
```
[W][component:490]: lvgl took a long time for an operation (460 ms)
```

LVGL prend 460ms pour rafraîchir l'écran, même avec 720P direct (sans PPA scaling).

**Cela signifie que le problème N'EST PAS le PPA, mais la configuration LVGL/Display!**

## Causes Probables

### 1. LVGL Buffer Trop Petit (ou absent)

Sans buffer configuré, LVGL utilise des mini-buffers qui nécessitent:
- **Plusieurs passes** pour rafraîchir l'écran
- Chaque passe = aller-retour DMA = TRÈS LENT
- 460ms pour 1280x720 = buffer trop petit!

### 2. Display Direct Mode Non Activé

M5Stack Tab5 avec display DPI a besoin du **direct mode** pour:
- Utiliser directement le framebuffer DPI
- Éviter les copies mémoire
- Rafraîchissement instantané

### 3. Canvas Déborde de l'Écran Roté

- Écran: 1280x720
- Rotation 270°: devient **720x1280**
- Canvas actuel: **800x480**
- **800 > 720 = DÉBORDEMENT de 80 pixels!**

LVGL doit gérer le clipping → ralentissement

## ✅ Solution Complète

### Étape 1: Ajouter Configuration LVGL Optimisée

Dans votre fichier YAML principal, ajoutez/modifiez la section `lvgl:`:

```yaml
lvgl:
  displays:
    - display_id: main_display

  # CRITIQUE: Augmenter buffer LVGL
  buffer_size: 100%        # OU: 50% minimum pour bonnes performances

  # Activer full refresh (plus rapide avec DPI)
  full_refresh: true       # Évite partial refresh lent

  # OPTIONNEL: Log level pour debug
  log_level: INFO

  pages:
    # ... vos pages existantes
```

**Explication:**
- `buffer_size: 100%` = Buffer = taille écran complète
- Rafraîchissement en **1 seule passe** au lieu de plusieurs
- Compatible avec DPI direct mode
- 460ms → ~20-30ms

**Compromis mémoire:**
- 1280x720x2 bytes (RGB565) = 1.76 MB
- ESP32-P4 a 8MB PSRAM → largement suffisant

**Si mémoire limitée:**
```yaml
buffer_size: 50%   # 2 passes au lieu de 10+
```

### Étape 2: Adapter Canvas à l'Écran Roté

Avec `rotation: 270°`, l'écran 1280x720 devient **720x1280**.

**Canvas maximum:**
```yaml
- canvas:
    id: camera_canvas
    width: 720      # ← Maximum pour écran roté (pas 800!)
    height: 480     # ← OK (480 < 1280)
    x: 0
    y: 0
```

**Configuration caméra correspondante:**
```yaml
mipi_dsi_cam:
  resolution: "1280x720"   # Ou "640x480"

  ppa_enabled: true
  output_width: 720        # ← Match canvas width!
  output_height: 480       # ← Match canvas height!

  # rotation: 270°  # Si besoin de rotation caméra
```

### Étape 3: Vérifier Display DPI

Votre configuration display semble correcte:
```yaml
display:
  - platform: mipi_dsi
    id: main_display
    model: M5Stack-Tab5
    update_interval: never    # ← Correct pour LVGL
    auto_clear_enabled: false # ← Correct pour LVGL
    rotation: 270
```

**Si problème persiste, essayez:**
```yaml
display:
  - platform: mipi_dsi
    id: main_display
    model: M5Stack-Tab5
    update_interval: never
    auto_clear_enabled: false
    rotation: 0              # ← Tester sans rotation
```

Puis gérez la rotation via PPA dans `mipi_dsi_cam`.

## 📊 Résultats Attendus

### AVANT (actuel)
```
[W] lvgl took a long time (460 ms)
FPS: 8.44 | capture: 22ms | canvas: 0.3ms
```

### APRÈS (avec buffer_size: 100%)
```
[I] lvgl refresh: ~20-30ms
FPS: ~25-30 | capture: 22ms | canvas: 0.3ms
```

**Gain:** 460ms → 20-30ms = **15× plus rapide!**

## 🔧 Configuration Complète Recommandée

```yaml
# ============================================================================
# DISPLAY
# ============================================================================
display:
  - platform: mipi_dsi
    id: main_display
    model: M5Stack-Tab5
    reset_pin:
      pi4ioe5v6408: pi4ioe1
      number: 4
    update_interval: never
    auto_clear_enabled: false
    rotation: 270

# ============================================================================
# LVGL
# ============================================================================
lvgl:
  displays:
    - display_id: main_display

  buffer_size: 100%       # ← AJOUTER: Buffer plein écran
  full_refresh: true      # ← AJOUTER: Refresh complet
  log_level: INFO

  pages:
    - id: camera_page
      bg_color: 0x000000

      widgets:
        - canvas:
            id: camera_canvas
            width: 720      # ← CHANGER: 800 → 720 (écran roté)
            height: 480
            x: 0
            y: 0

# ============================================================================
# CAMÉRA
# ============================================================================
mipi_dsi_cam:
  id: tab5_cam
  sensor_type: sc202cs
  i2c_id: bsp_bus
  sensor_addr: 0x36
  resolution: "1280x720"   # ← Option 1: 720P natif
  # resolution: "640x480"  # ← Option 2: VGA (si 720P lent)
  pixel_format: RGB565
  framerate: 30

  ppa_enabled: true
  output_width: 720        # ← Match canvas!
  output_height: 480

# ============================================================================
# LVGL CAMERA DISPLAY
# ============================================================================
lvgl_camera_display:
  id: camera_display
  camera_id: tab5_cam
  canvas_id: camera_canvas
  update_interval: 33ms    # 30 FPS
```

## 🎯 Tests à Faire

### Test #1: Buffer Size

1. Ajoutez `buffer_size: 100%` à votre LVGL config
2. Recompilez et flashez
3. Vérifiez les logs - le warning "lvgl took a long time" devrait **disparaître**
4. FPS devrait monter à ~25-30

### Test #2: Canvas Width

1. Changez canvas `width: 720` (au lieu de 800)
2. Changez caméra `output_width: 720`
3. Recompilez
4. Devrait éliminer tout débordement

### Test #3: Résolution Caméra

Testez différentes résolutions natives (sans scaling):

**Option A - VGA:**
```yaml
resolution: "640x480"
ppa_enabled: false
```

**Option B - 720P:**
```yaml
resolution: "1280x720"
output_width: 720
output_height: 480  # Crop/resize
```

**Option C - SVGA (si supporté):**
```yaml
resolution: "800x600"
output_width: 720
output_height: 480
```

## ⚠️ Diagnostic Supplémentaire

Si après avoir ajouté `buffer_size: 100%`, le FPS reste bas:

1. **Vérifiez mémoire PSRAM:**
   - 1280x720x2 = 1.76 MB pour buffer
   - ESP32-P4 doit avoir assez de PSRAM libre

2. **Testez sans rotation display:**
   ```yaml
   display:
     rotation: 0   # Au lieu de 270
   ```

3. **Vérifiez DPI clock:**
   - M5Stack Tab5 devrait utiliser ~30MHz pixel clock
   - Vérifiez les logs au boot du display

4. **Collectez nouveaux logs:**
   Envoyez les logs complets après avoir appliqué `buffer_size: 100%`

## 📝 Checklist

- [ ] Ajouter `buffer_size: 100%` dans `lvgl:`
- [ ] Ajouter `full_refresh: true` dans `lvgl:`
- [ ] Changer canvas `width: 720` (au lieu de 800)
- [ ] Changer caméra `output_width: 720`
- [ ] Recompiler et flasher
- [ ] Vérifier que warning "lvgl took a long time" disparaît
- [ ] Vérifier FPS monte à 25-30

**Si vous me montrez votre fichier YAML principal, je peux vous donner la configuration exacte à utiliser!**
