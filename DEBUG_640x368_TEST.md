# Test et Debug: Format 640×368 sur OV02C10

## ✅ Le Format 640×368 EST Implémenté

Le format **640×368 existe** dans le code et **DOIT fonctionner**. Voici comment le tester correctement.

---

## 🧪 Test Étape par Étape

### Étape 1: Configuration Minimale

Utilisez cette configuration **SANS rotation** d'abord pour isoler le problème:

```yaml
esp_cam_sensor:
  id: tab5_cam
  i2c_id: i2c_bus
  sensor_type: ov02c10
  resolution: "640x368"      # ← Format à tester
  pixel_format: "RGB565"
  framerate: 30
  jpeg_quality: 15
  rotation: 0                # ← IMPORTANT: Commencer SANS rotation
  mirror_x: false
  mirror_y: false
  ppa_enabled: false         # ← Désactiver PPA pour commencer

lvgl:
  - canvas:
      id: camera_canvas
      width: 640        # ← Dimensions sans rotation
      height: 368
      x: 80             # Centré: (800-640)/2
      y: 56             # Centré: (480-368)/2
```

### Étape 2: Vérifier les Logs

Après démarrage, cherchez ces logs **EXACTS**:

```
[esp_cam_sensor_camera:857] ✅ Using CUSTOM format: 640x368 RAW10 @ 30fps (near 16:9, ~2% crop, 16-byte aligned!)
[esp_cam_sensor_camera:869] Custom format applied successfully!
[esp_cam_sensor_camera:870]    Sensor registers configured for native 640x368
[esp_cam_sensor_camera:875]    Actual output dimensions after rotation: 640x368
```

**Si vous voyez ces logs** → Le format est reconnu et appliqué ✅

**Si vous NE voyez PAS ces logs** → Envoyez-moi les logs complets

---

## 🔍 Problèmes Possibles

### Problème 1: Format Non Reconnu

**Symptôme:**
```
[esp_cam_sensor_camera:137] Résolution '640x368' non reconnue, fallback 1280x720
```

**Solution:**
- Vérifiez que vous utilisez bien `resolution: "640x368"` (avec guillemets)
- PAS `resolution: 640x368` (sans guillemets)

### Problème 2: VIDIOC_S_SENSOR_FMT Échoue

**Symptôme:**
```
[esp_cam_sensor_camera:866] VIDIOC_S_SENSOR_FMT failed: Invalid argument
```

**Solution:**
- Le kernel ne supporte pas ce format
- Vérifiez ESP-IDF version (besoin 5.3+)
- Envoyez-moi les logs complets

### Problème 3: Image Noire ou Corrompue

**Symptôme:**
- Format reconnu mais canvas noir
- Image pixélisée/corrompue

**Solution:**
- Testez SANS rotation d'abord (`rotation: 0`)
- Testez SANS PPA (`ppa_enabled: false`)
- Canvas doit être **exactement** 640×368

### Problème 4: Problème avec Rotation 270°

**Symptôme:**
- Fonctionne SANS rotation
- Ne fonctionne PAS avec `rotation: 270`

**Solution:**
```yaml
# Étape 1: Tester rotation 270° SANS PPA
rotation: 270
ppa_enabled: false
canvas:
  width: 368    # ← Inversé après rotation
  height: 640

# Étape 2: Si ça marche, activer PPA
ppa_enabled: true
canvas:
  width: 368
  height: 390   # ← Crop vertical par PPA
```

---

## 📋 Checklist de Debug

Cochez ce qui fonctionne:

- [ ] Format reconnu dans les logs (`✅ Using CUSTOM format: 640x368`)
- [ ] Custom format appliqué (`Custom format applied successfully`)
- [ ] Image visible (pas noire) SANS rotation
- [ ] Image visible (pas noire) AVEC rotation 270°
- [ ] Image visible AVEC rotation 270° + PPA resize

---

## 🐛 Rapport de Bug

Si 640×368 ne fonctionne toujours pas, envoyez-moi:

### 1. Configuration YAML Complète

```yaml
esp_cam_sensor:
  # ... votre config exacte
```

### 2. Logs Complets

Cherchez dans les logs:
```
grep -E "640x368|CUSTOM format|VIDIOC_S_SENSOR_FMT" votre_log.txt
```

### 3. Comportement Observé

- [ ] Format non reconnu dans les logs
- [ ] Format reconnu mais canvas noir
- [ ] Format reconnu mais image corrompue/pixélisée
- [ ] Format reconnu mais crash/reboot
- [ ] Format reconnu mais watchdog timeout
- [ ] Fonctionne SANS rotation, ne fonctionne PAS AVEC rotation
- [ ] Autre: _______________

---

## 💡 Alternative Temporaire

En attendant le debug de 640×368, utilisez **1920×1080** qui fonctionne à coup sûr:

```yaml
esp_cam_sensor:
  resolution: "1920x1080"  # ← Fonctionne toujours
  rotation: 270
  ppa_enabled: true

lvgl:
  - canvas:
      width: 480
      height: 390    # ← Votre canvas actuel
```

---

## 🔧 Tests Additionnels

### Test A: Sans PPA ni Rotation

```yaml
resolution: "640x368"
rotation: 0
ppa_enabled: false
canvas: 640×368
```

### Test B: Avec Rotation, Sans PPA

```yaml
resolution: "640x368"
rotation: 270
ppa_enabled: false
canvas: 368×640
```

### Test C: Avec Rotation et PPA

```yaml
resolution: "640x368"
rotation: 270
ppa_enabled: true
canvas: 368×390  # ou 480×390 avec upscale
```

---

## 📊 Vérification du Code

Le format 640×368 est défini ici:

- **Code C++:** `components/esp_cam_sensor/esp_cam_sensor_camera.cpp:855-857`
- **Registres sensor:** `components/esp_cam_sensor/sensor/ov02c10/private_include/ov02c10_settings.h:1228-1350`
- **Header:** `components/esp_cam_sensor/ov02c10_custom_formats.h:47`

**Parsing de la résolution:**
```c
// components/esp_cam_sensor/esp_cam_sensor_camera.cpp:107
if (sscanf(res.c_str(), "%ux%u", &pw, &ph) == 2 && pw > 0 && ph > 0) {
    w = pw; h = ph; return true;  // ← "640x368" devrait matcher ICI
}
```

---

## ✅ Prochaines Étapes

1. **Testez** avec la config minimale (Test A)
2. **Envoyez-moi** les logs complets si ça ne marche pas
3. **Indiquez** à quelle étape ça échoue (Test A/B/C)
4. Je **corrigerai** le problème spécifique

Le format 640×368 **DOIT** fonctionner - il est correctement implémenté. S'il ne fonctionne pas, c'est un bug spécifique que nous allons corriger ensemble !
