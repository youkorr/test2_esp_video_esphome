# Fix: Lignes Transparentes et Compression avec 640×368

## 🔍 Problème Identifié

Vous utilisez:
```yaml
resolution: "640x368"
rotation: 270
canvas: 480×390
```

**Ce qui se passe:**
- Capture: 640×368
- Rotation 270°: → **368×640**
- PPA essaie d'upscaler: 368→480 horizontal + crop 640→390 vertical
- **PROBLÈME:** L'upscale horizontal 368→480 (+30%) crée des lignes transparentes et compression

**Cause:** Le PPA ne gère pas bien l'upscale horizontal avec rotation 270°. Le stride mémoire ne correspond pas aux dimensions du canvas.

---

## ✅ SOLUTION 1: Adapter le Canvas aux Dimensions Exactes (RECOMMANDÉ)

Le canvas DOIT correspondre aux dimensions APRÈS rotation, SANS upscale.

### Configuration Correcte

```yaml
esp_cam_sensor:
  id: tab5_cam
  i2c_id: i2c_bus
  sensor_type: ov02c10
  resolution: "640x368"      # ← Format 16:9, 98% FOV
  pixel_format: "RGB565"
  framerate: 30
  jpeg_quality: 15
  rotation: 270              # → Devient 368×640
  mirror_x: true
  mirror_y: false
  crop_offset_x: 0
  ppa_enabled: true

lvgl:
  pages:
    - id: camera_page
      widgets:
        - canvas:
            id: camera_canvas
            width: 368       # ← DOIT être 368 (pas 480!)
            height: 390      # ← PPA crop 640→390 OK
            x: 216           # ← Centré: (800-368)/2 = 216
            y: 40
            bg_color: 0x000000
```

### Pourquoi 368 et pas 480?

| Dimension | Après Rotation | PPA Resize | Canvas | Résultat |
|-----------|----------------|------------|--------|----------|
| **Largeur** | 368 | **368→368** | 368 | ✅ **Pas d'upscale** |
| **Hauteur** | 640 | **640→390** | 390 | ✅ **Downscale OK** |

**Le PPA peut downscaler (640→390) mais l'upscale (368→480) cause des problèmes!**

---

## ✅ SOLUTION 2: Utiliser 1920×1080 pour Canvas 480×390

Si vous DEVEZ absolument garder canvas 480×390, utilisez 1920×1080:

```yaml
esp_cam_sensor:
  id: tab5_cam
  resolution: "1920x1080"    # ← Full sensor, 100% FOV
  rotation: 270              # → Devient 1080×1920
  ppa_enabled: true

# Après rotation: 1080×1920
# PPA downscale: 1080→480, 1920→390 (downscale seulement, pas d'upscale)

lvgl:
  - canvas:
      width: 480        # ← Garde votre config actuelle
      height: 390
      x: 30
      y: 40
```

**Avantages:**
- ✅ PAS de lignes transparentes (downscale seulement)
- ✅ PAS de compression (dimensions exactes)
- ✅ 100% du FOV (vs 98% pour 640×368)

**Inconvénient:**
- Plus de RAM (~6 MB)

---

## ✅ SOLUTION 3: Créer Format 480×270 (Parfait pour Canvas 480×390)

Je peux créer un format **480×270** qui après rotation 270° donne **270×480**, puis PPA crop à 270×390.

**Attendez** - cela nécessite d'ajouter un nouveau format dans le code.

Préférez **Solution 1** (canvas 368×390) ou **Solution 2** (1920×1080).

---

## 🔧 Diagnostic du Problème

### Stride Mémoire

Les lignes transparentes viennent du **stride incorrect**:

```
Capture: 640×368 pixels
Rotation 270°: 368×640 pixels
RGB565: 2 bytes par pixel

Stride attendu: 368 × 2 = 736 bytes par ligne
Canvas config: 480 pixels de large
Stride canvas: 480 × 2 = 960 bytes

PROBLÈME: 960 ≠ 736 → Décalage mémoire → Lignes transparentes
```

### Pourquoi ça ne marche pas avec upscale?

Le PPA peut:
- ✅ **Downscale** (réduction) sans problème
- ❌ **Upscale** (agrandissement) avec rotation → **problèmes d'alignement**

---

## 📊 Comparaison des Solutions

| Solution | Canvas | FOV | Lignes Transparentes | RAM | Complexité |
|----------|--------|-----|---------------------|-----|-----------|
| **1. Canvas 368×390** | 368×390 | 98% | ✅ **Aucune** | 0.9 MB | Simple |
| **2. 1920×1080** | 480×390 | 100% | ✅ **Aucune** | 6.2 MB | Simple |
| **3. Actuel (368→480 upscale)** | 480×390 | 98% | ❌ **Oui** | 0.9 MB | - |

---

## 🚀 Migration Rapide (Solution 1)

### Étape 1: Changez le canvas

```yaml
# AVANT:
- canvas:
    width: 480
    height: 390
    x: 30

# APRÈS:
- canvas:
    width: 368       # ← Réduit de 480 → 368
    height: 390      # ← Inchangé
    x: 216           # ← Centré: (800-368)/2
```

### Étape 2: Repositionnez les boutons

Vos boutons sont à `x: 690`. Ils sont maintenant plus loin du canvas (plus d'espace):

```yaml
# Option A: Garder position (plus d'espace)
- button:
    x: 690  # ← Inchangé

# Option B: Rapprocher du canvas
- button:
    x: 620  # ← Plus proche
```

### Étape 3: Testez

Les lignes transparentes doivent **disparaître** complètement!

---

## 🚀 Migration Alternative (Solution 2)

Si vous voulez garder canvas 480×390:

```yaml
# CHANGEZ SEULEMENT CECI:
esp_cam_sensor:
  resolution: "1920x1080"  # ← De "640x368" à "1920x1080"

# TOUT LE RESTE IDENTIQUE (canvas, boutons, etc.)
```

---

## 🐛 Vérification

Après le changement, vérifiez:

1. ✅ **Pas de lignes transparentes** au milieu
2. ✅ **Image complète** (pas divisée/compressée)
3. ✅ **Proportions correctes** (pas déformé)
4. ✅ **98% ou 100% FOV** visible

---

## 💡 Pourquoi 640×368 "ne fonctionnait jamais"

Maintenant on comprend! Le format **fonctionnait** mais:
- ❌ Avec upscale 368→480: lignes transparentes
- ❌ Image compressée/divisée
- ❌ Rendu incorrect

**C'était un problème de dimensions de canvas**, pas du format lui-même!

---

## 📝 Résumé

**Problème:** PPA ne peut pas upscaler 368→480 horizontal correctement avec rotation

**Solution:**
- **Recommandé:** Canvas 368×390 (Solution 1)
- **Si besoin 480×390:** 1920×1080 (Solution 2)

**Les deux solutions éliminent les lignes transparentes!**
