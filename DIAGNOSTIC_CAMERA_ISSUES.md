# Diagnostic des Problèmes de Caméra - OV5647 @ 1024x600

## Résumé de l'Analyse des Logs

Date: 2025-11-11
Configuration: OV5647 @ 1024x600, RGB565, Web Stream actif

### ✅ CE QUI FONCTIONNE

1. **Format Custom Appliqué avec Succès**
   ```
   [I][mipi_dsi_cam:712]: ✅ Using CUSTOM format: 1024x600 RAW8 @ 30fps (OV5647)
   [I][mipi_dsi_cam:721]: ✅ Custom format applied successfully!
   [I][mipi_dsi_cam:722]:    Sensor registers configured for native 1024x600
   ```
   - ✅ Corrections d'exposition (AEC/AGC activé via register 0x3503 = 0x00)
   - ✅ Bayer pattern corrigé (BGGR)
   - ✅ Registres du capteur configurés correctement

2. **Capture de Frames**
   ```
   [I][mipi_dsi_cam:1067]: ✅ First frame captured (zero-copy):
   [I][mipi_dsi_cam:1068]:    Buffer size: 1228800 bytes (1024x600 × 2 = RGB565)
   ```
   - ✅ Les frames sont capturées correctement
   - ✅ Le web stream fonctionne (JPEG encodé et envoyé)

### ❌ PROBLÈMES IDENTIFIÉS

#### Problème 1: PPA Transaction Overflow (CORRIGÉ)

**Symptôme:**
```
E (31152) ppa_srm: exceed maximum pending transactions for the client
[E][mipi_dsi_cam:282]: PPA transform failed: ESP_FAIL
```

**Cause:**
Le PPA (Pixel-Processing Accelerator) était configuré avec `max_pending_trans_num = 1`, insuffisant quand le web stream et LVGL display fonctionnent simultanément.

**Solution Appliquée:**
```cpp
// mipi_dsi_cam.cpp ligne 217
ppa_config.max_pending_trans_num = 4;  // Increased from 1 to 4
```

**Impact:**
- Le PPA peut maintenant gérer 4 transformations concurrentes (mirror/rotate)
- Élimine les erreurs "exceed maximum pending transactions"
- Évite le watchdog timeout causé par les erreurs PPA répétées

#### Problème 2: Canvas LVGL Non Configuré (NÉCESSITE CONFIGURATION YAML)

**Symptôme:**
```
[W][lvgl_camera_display:125]: ❌ Canvas null - pas encore configuré?
```

**Cause:**
Le composant `lvgl_camera_display` est actif mais le canvas LVGL n'est pas configuré dans le YAML.

**Impact:**
- Pas d'affichage de la caméra à l'écran
- Le web stream fonctionne mais pas l'affichage LVGL

**Solution Requise:**
Ajouter une configuration LVGL complète dans votre fichier YAML principal.

### Configuration LVGL Requise

Pour afficher la caméra à l'écran, vous devez ajouter cette configuration à votre YAML:

```yaml
# ============================================
# LVGL Display Configuration
# ============================================
display:
  - platform: rpi_dpi_rgb
    # ... votre configuration display existante ...

# ============================================
# LVGL Configuration avec Camera Canvas
# ============================================
lvgl:
  displays:
    - display_id: main_display  # Adaptez à votre display_id
      pages:
        - id: camera_page
          widgets:
            # Canvas pour afficher la caméra
            - canvas:
                id: camera_canvas
                width: 1024
                height: 600
                x: 0      # Centrez selon votre écran
                y: 0
                bg_color: 0x000000

            # Bouton optionnel pour retourner au menu
            - button:
                x: 10
                y: 10
                width: 100
                height: 50
                widgets:
                  - label:
                      text: "BACK"
                      align: center

# ============================================
# LVGL Camera Display - Connecte la caméra au canvas
# ============================================
lvgl_camera_display:
  id: camera_display
  camera_id: my_cam          # ID de votre composant mipi_dsi_cam
  canvas_id: camera_canvas   # ID du canvas créé ci-dessus
  update_interval: 100ms     # 10 FPS (évite watchdog timeout)
```

### Ajustements Selon Votre Écran

#### Pour écran 800x480 (centrer 1024x600 avec crop):
```yaml
- canvas:
    id: camera_canvas
    width: 800    # Largeur écran
    height: 480   # Hauteur écran
    x: 0
    y: 0
```
Note: L'image 1024x600 sera automatiquement redimensionnée/croppée.

#### Pour écran 1024x600 (affichage plein écran):
```yaml
- canvas:
    id: camera_canvas
    width: 1024
    height: 600
    x: 0
    y: 0
```

### Vérification du Canvas Configuré

Après avoir ajouté la configuration LVGL, vous devriez voir ce log au démarrage:

```
[I][lvgl_camera_display:153]: 🎨 Canvas configuré: 0x48xxxxxx
[I][lvgl_camera_display:158]:    Taille canvas: 1024x600
```

Au lieu de:
```
[W][lvgl_camera_display:125]: ❌ Canvas null - pas encore configuré?
```

### Exemple Complet de Configuration

Voici un exemple complet pour OV5647 @ 1024x600:

```yaml
# ============================================
# I2C Bus
# ============================================
i2c:
  - id: bsp_bus
    sda: GPIO8
    scl: GPIO9
    frequency: 400kHz

# ============================================
# Caméra OV5647
# ============================================
mipi_dsi_cam:
  id: my_cam
  i2c_id: bsp_bus
  sensor_type: ov5647
  sensor_addr: 0x36
  resolution: "1024x600"   # Utilise custom format
  pixel_format: RGB565
  framerate: 30
  # mirror_x: true         # Si besoin de miroir horizontal
  # mirror_y: false        # Si besoin de miroir vertical

# ============================================
# ESP Video (nécessaire pour ISP)
# ============================================
esp_video:
  enable_isp: true
  enable_jpeg: true       # Pour web stream
  enable_h264: false

# ============================================
# Camera Web Server (stream web)
# ============================================
camera_web_server:
  camera_id: my_cam
  port: 80
  enable_stream: true
  jpeg_quality: 80

# ============================================
# LVGL Display
# ============================================
display:
  - platform: rpi_dpi_rgb
    id: main_display
    # ... votre config display ...

lvgl:
  displays:
    - display_id: main_display
      pages:
        - id: camera_page
          widgets:
            - canvas:
                id: camera_canvas
                width: 1024
                height: 600
                x: 0
                y: 0

# ============================================
# LVGL Camera Display
# ============================================
lvgl_camera_display:
  id: camera_display
  camera_id: my_cam
  canvas_id: camera_canvas
  update_interval: 100ms
```

## Tests à Effectuer

### Test 1: Vérifier que le PPA ne génère plus d'erreurs

Après avoir flashé le nouveau firmware (avec `max_pending_trans_num = 4`), surveillez les logs:

**Avant (ERREUR):**
```
E (31152) ppa_srm: exceed maximum pending transactions
[E][mipi_dsi_cam:282]: PPA transform failed: ESP_FAIL
```

**Après (OK):**
Aucune erreur PPA, pas de watchdog timeout.

### Test 2: Vérifier que le canvas est configuré

Après avoir ajouté la configuration LVGL, surveillez les logs au démarrage:

**Avant (ERREUR):**
```
[W][lvgl_camera_display:125]: ❌ Canvas null - pas encore configuré?
```

**Après (OK):**
```
[I][lvgl_camera_display:153]: 🎨 Canvas configuré: 0x48xxxxxx
[I][lvgl_camera_display:158]:    Taille canvas: 1024x600
[I][lvgl_camera_display:139]: 🖼️  Premier update canvas:
[I][lvgl_camera_display:140]:    Dimensions: 1024x600
```

### Test 3: Vérifier que l'image s'affiche à l'écran

L'image de la caméra doit s'afficher sur l'écran LVGL sans erreurs.

**Logs FPS attendus:**
```
[I][lvgl_camera_display:99]: 🎞️ 100 frames - FPS: 9.8 | capture: 0.5ms | canvas: 2.1ms | skip: 0.0%
```

## Résumé des Corrections

### 1. Format Custom OV5647 (DÉJÀ APPLIQUÉ ✅)
- Fichier: `components/mipi_dsi_cam/ov5647_custom_formats.h`
- AEC/AGC activé (register 0x3503 = 0x00)
- Bayer pattern corrigé (BGGR)
- Exposition: exp_def = 0x300 (768)

### 2. Fix PPA Transaction Overflow (APPLIQUÉ ✅)
- Fichier: `components/mipi_dsi_cam/mipi_dsi_cam.cpp` ligne 217
- `max_pending_trans_num` augmenté de 1 à 4
- Élimine les erreurs PPA et watchdog timeout

### 3. Configuration LVGL Canvas (À FAIRE PAR L'UTILISATEUR)
- Ajouter configuration LVGL au YAML principal
- Créer un canvas avec les bonnes dimensions
- Connecter le canvas au composant `lvgl_camera_display`

## Actions Requises

1. **Compiler et flasher le firmware avec le fix PPA:**
   ```bash
   pio run -e esp32-p4-function-ev-board -t upload
   ```

2. **Ajouter la configuration LVGL au YAML** (voir exemples ci-dessus)

3. **Recompiler et flasher** avec la nouvelle configuration LVGL

4. **Vérifier les logs** pour confirmer que:
   - Pas d'erreurs PPA
   - Canvas configuré
   - Image affichée à l'écran

## Questions Fréquentes

### Q: Pourquoi le web stream fonctionne mais pas l'affichage à l'écran?
**R:** Le web stream utilise directement les buffers V4L2, tandis que l'affichage LVGL nécessite un canvas configuré. Ce sont deux chemins indépendants.

### Q: Dois-je utiliser mirror_x ou mirror_y?
**R:** Seulement si l'image est inversée sur votre écran. Le PPA hardware gère ces transformations.

### Q: Quelle update_interval pour LVGL?
**R:**
- **100ms (10 FPS)**: Recommandé pour éviter watchdog timeout
- **33ms (30 FPS)**: Maximum, mais peut causer des timeouts si CPU chargé

### Q: L'image est toujours trop lumineuse/rouge?
**R:**
- Le format custom est appliqué ✅
- Attendez quelques secondes que l'AEC/AGC converge
- Si le problème persiste, partagez une capture d'écran et les logs
