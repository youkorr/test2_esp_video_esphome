# Guide des Pages LVGL pour Caméras MIPI

Ce guide explique comment configurer les pages LVGL simplifiées pour chaque sensor MIPI.

## 📋 Vue d'ensemble

Trois fichiers de configuration LVGL ont été créés, un pour chaque sensor:

| Fichier | Sensor | Résolutions | Canvas |
|---------|--------|-------------|--------|
| `LVGL_CAMERA_PAGE_OV02C10.yaml` | OV02C10 | 800x480, 1280x800 | 800x480 plein écran |
| `LVGL_CAMERA_PAGE_OV5647.yaml` | OV5647 | 640x480 (VGA), 1024x600 | 640x480 centré ou 1024x600 plein |
| `LVGL_CAMERA_PAGE_SC202CS.yaml` | SC202CS | 640x480 (VGA), 720P | 640x480 centré ou 800x480 plein |

## 🎯 Interface Simplifiée

Toutes les pages utilisent la même interface minimaliste:

```
┌─────────────────────────────────────┐
│                                     │
│         [CANVAS CAMÉRA]             │
│                                     │
│                                     │
│  [BACK]                             │
│  [START]                            │
│  [STOP]                             │
│  [INFO]                             │
│                                     │
│                      [STATUS]       │
└─────────────────────────────────────┘
```

### 4 Boutons de Contrôle

1. **BACK** (gris) - Arrête le streaming et retourne à la page d'accueil
2. **START** (vert) - Démarre le streaming vidéo
3. **STOP** (rouge) - Arrête le streaming
4. **INFO** (bleu) - Affiche les informations dans les logs ESP

## 🔄 Event `on_load` - IMPORTANT!

Toutes les pages utilisent l'événement `on_load` pour configurer automatiquement le canvas quand la page se charge:

```yaml
lvgl:
  pages:
    - id: camera_page
      bg_color: 0x000000

      on_load:
        - lambda: |-
            ESP_LOGI("camera", "📸 Page caméra chargée");

            // Récupérer le canvas
            auto canvas = id(camera_canvas);
            if (canvas != nullptr) {
              // Définir taille et position
              lv_obj_set_size(canvas, 800, 480);
              lv_obj_set_pos(canvas, 0, 0);

              // CRITIQUE: lier le canvas à la caméra
              id(tab5_cam).configure_canvas(canvas);

              ESP_LOGI("camera", "✅ Canvas configuré");
            }

            // Optionnel: auto-démarrage du streaming
            // if (id(tab5_cam).start_streaming()) {
            //   lv_label_set_text(id(status_label), "LIVE");
            // }
```

### Pourquoi `on_load` est Important

1. **Configuration automatique** - Le canvas est configuré dès le chargement de la page
2. **Pas besoin de `display.lambda`** - Tout se fait dans la page LVGL
3. **Flexibilité** - Vous pouvez changer la taille du canvas dynamiquement
4. **Auto-start optionnel** - Décommentez le code pour démarrer automatiquement le streaming

### Avec ou Sans Auto-Start?

**Sans auto-start (par défaut):**
- L'utilisateur doit appuyer sur **START** pour démarrer
- Plus de contrôle
- Économise de la batterie/CPU

**Avec auto-start (décommentez le code):**
- La vidéo démarre automatiquement au chargement de la page
- Expérience plus fluide
- Consomme plus de ressources

## 📐 Configurations par Sensor

### OV02C10 - Custom Formats

**Écran: 800x480**

```yaml
mipi_dsi_cam:
  id: tab5_cam
  i2c_id: bsp_bus
  sensor_type: ov02c10
  xclk_pin: GPIO36
  xclk_freq: 24000000
  sensor_addr: 0x3C
  resolution: "800x480"      # Custom format
  pixel_format: RGB565
  framerate: 30

lvgl:
  pages:
    - id: camera_page
      bg_color: 0x000000
      widgets:
        - canvas:
            id: camera_canvas
            width: 800          # Plein écran
            height: 480
            x: 0
            y: 0
        # ... boutons ...
```

**Écran: 1280x800**

```yaml
mipi_dsi_cam:
  resolution: "1280x800"     # Custom format

lvgl:
  pages:
    - id: camera_page
      widgets:
        - canvas:
            width: 1280        # Plein écran
            height: 800
            x: 0
            y: 0
```

**Caractéristiques:**
- Bayer: BGGR
- MIPI: 2-lane
- Mémoire 800x480: ~768 KB
- Mémoire 1280x800: ~2 MB

---

### OV5647 - Custom Formats

**Version 1: VGA 640x480 (écran 800x480)**

```yaml
mipi_dsi_cam:
  id: tab5_cam
  i2c_id: bsp_bus
  sensor_type: ov5647
  xclk_pin: GPIO36
  xclk_freq: 24000000
  sensor_addr: 0x36
  resolution: "VGA"          # Custom format 640x480
  pixel_format: RGB565
  framerate: 30

lvgl:
  pages:
    - id: camera_page_vga
      widgets:
        - canvas:
            width: 640         # Centré sur écran 800x480
            height: 480
            x: 80              # (800-640)/2 = 80
            y: 0
```

**Version 2: 1024x600 (écran 7" Waveshare)**

```yaml
mipi_dsi_cam:
  resolution: "1024x600"     # Custom format

lvgl:
  pages:
    - id: camera_page_1024x600
      widgets:
        - canvas:
            width: 1024        # Plein écran
            height: 600
            x: 0
            y: 0
```

**Caractéristiques:**
- Bayer: GBRG
- MIPI: 2-lane
- Binning VGA: 4x4
- Binning 1024x600: 2x2
- Mémoire VGA: ~614 KB
- Mémoire 1024x600: ~1.2 MB

---

### SC202CS - Custom Format VGA

**Version 1: VGA 640x480 (recommandé pour petits écrans)**

```yaml
mipi_dsi_cam:
  id: tab5_cam
  i2c_id: bsp_bus
  sensor_type: sc202cs
  xclk_pin: GPIO36
  xclk_freq: 24000000
  sensor_addr: 0x30
  resolution: "VGA"          # Custom format 640x480
  pixel_format: RGB565
  framerate: 30

lvgl:
  pages:
    - id: camera_page_vga
      widgets:
        - canvas:
            width: 640         # Centré sur écran 800x480
            height: 480
            x: 80              # (800-640)/2 = 80
            y: 0
```

**Version 2: 720P (écran 800x480)**

```yaml
mipi_dsi_cam:
  resolution: "720P"         # Format standard

lvgl:
  pages:
    - id: camera_page_720p
      widgets:
        - canvas:
            width: 800         # Plein écran (1280x720 downscaled)
            height: 480
            x: 0
            y: 0
```

**Caractéristiques:**
- Bayer: BGGR
- MIPI: **1-lane** (différent des autres!)
- Binning VGA: 2x2
- Mémoire VGA: ~614 KB
- Mémoire 720P: ~1.8 MB

---

## 🔧 Configuration Complète

### Étape 1: Définir la Caméra

```yaml
mipi_dsi_cam:
  id: tab5_cam
  i2c_id: bsp_bus
  sensor_type: ov02c10        # ou ov5647, sc202cs
  xclk_pin: GPIO36
  xclk_freq: 24000000
  sensor_addr: 0x3C           # 0x3C (OV02C10), 0x36 (OV5647), 0x30 (SC202CS)
  resolution: "800x480"       # Adapter selon le sensor
  pixel_format: RGB565
  framerate: 30
```

### Étape 2: Créer la Page LVGL

Copiez le contenu du fichier YAML correspondant à votre sensor:
- `LVGL_CAMERA_PAGE_OV02C10.yaml`
- `LVGL_CAMERA_PAGE_OV5647.yaml`
- `LVGL_CAMERA_PAGE_SC202CS.yaml`

### Étape 3: Configurer le Canvas dans Display

```yaml
display:
  - platform: ili9xxx
    model: st7796
    id: camera_display
    # ... autres paramètres ...
    lambda: |-
      // Configurer le canvas au démarrage
      id(tab5_cam).configure_canvas(id(camera_canvas));
```

### Étape 4: Créer le Lien Page d'Accueil → Caméra

```yaml
lvgl:
  pages:
    - id: page_home
      widgets:
        - button:
            text: "CAMERA"
            on_click:
              then:
                - lvgl.page.show: camera_page
```

---

## ✅ Règles Importantes

### 1. Canvas = Résolution Caméra

Le canvas LVGL **DOIT** avoir exactement la même taille que la résolution caméra:

```yaml
# ✅ CORRECT
mipi_dsi_cam:
  resolution: "800x480"

lvgl:
  widgets:
    - canvas:
        width: 800
        height: 480

# ❌ INCORRECT - Watchdog timeout!
mipi_dsi_cam:
  resolution: "800x480"

lvgl:
  widgets:
    - canvas:
        width: 0            # Canvas non créé!
        height: 0
```

### 2. Canvas ≤ Taille Écran

Le canvas ne peut pas être plus grand que l'écran:

```yaml
# ✅ CORRECT - Écran 800x480
- canvas:
    width: 640   # < 800
    height: 480  # = 480
    x: 80        # Centré

# ❌ INCORRECT - Canvas trop grand!
- canvas:
    width: 1280  # > 800
    height: 720  # > 480
```

### 3. Centrage du Canvas

Si le canvas est plus petit que l'écran, centrez-le:

```yaml
# Canvas 640x480 sur écran 800x480
- canvas:
    width: 640
    height: 480
    x: 80        # (800-640)/2 = 80
    y: 0
```

### 4. Pas d'Overlay sur le Canvas

Pour de meilleures performances, évitez de placer des widgets par-dessus le canvas:

```yaml
# ✅ CORRECT - Boutons sur le côté
- canvas:
    x: 80
    y: 0
    width: 640
    height: 480

- button:
    x: 5         # À gauche du canvas
    y: 10

# ⚠️  MOINS OPTIMAL - Boutons par-dessus
- canvas:
    x: 0
    y: 0
    width: 800
    height: 480

- button:
    x: 10        # Sur le canvas (ralentit le refresh)
    y: 10
```

---

## 📊 Tableau Comparatif

| Sensor | Résolution | Canvas | Bayer | MIPI | Mémoire | Usage |
|--------|------------|--------|-------|------|---------|-------|
| OV02C10 | 800x480 | 800x480 plein | BGGR | 2-lane | 768 KB | Écran 4.3-5" |
| OV02C10 | 1280x800 | 1280x800 plein | BGGR | 2-lane | 2 MB | Écran 7-10" |
| OV5647 | VGA | 640x480 centré | GBRG | 2-lane | 614 KB | Écran 4.3-5" |
| OV5647 | 1024x600 | 1024x600 plein | GBRG | 2-lane | 1.2 MB | Écran 7" |
| SC202CS | VGA | 640x480 centré | BGGR | 1-lane | 614 KB | Écran 4.3-5" |
| SC202CS | 720P | 800x480 plein | BGGR | 1-lane | 1.8 MB | Écran 4.3-5" |

---

## 🚨 Dépannage

### Problème: Watchdog Timeout

**Symptôme:** ESP redémarre après 5 secondes

**Cause:** Canvas LVGL non créé ou taille incorrecte

**Solution:**
```yaml
# Vérifiez que le canvas est bien créé avec la bonne taille
lvgl:
  widgets:
    - canvas:
        id: camera_canvas
        width: 800       # = résolution caméra
        height: 480
```

### Problème: Écran Noir

**Symptôme:** Canvas noir, pas d'image

**Causes possibles:**
1. Streaming pas démarré → Appuyez sur **START**
2. Canvas pas configuré → Vérifiez que `on_load` appelle `configure_canvas()`
3. Résolution incorrecte → Vérifiez que custom format existe

**Solution:**
```yaml
lvgl:
  pages:
    - id: camera_page
      on_load:
        - lambda: |-
            auto canvas = id(camera_canvas);
            if (canvas != nullptr) {
              id(tab5_cam).configure_canvas(canvas);
            }
```

### Problème: Boutons ne Répondent Pas

**Cause:** IDs incorrects

**Solution:** Vérifiez que tous les IDs correspondent:
```yaml
mipi_dsi_cam:
  id: tab5_cam          # Même ID partout!

lvgl:
  widgets:
    - canvas:
        id: camera_canvas   # Même ID partout!
```

---

## 📝 Exemple Complet OV02C10 800x480

Voici un exemple complet fonctionnel:

```yaml
# Configuration caméra
mipi_dsi_cam:
  id: tab5_cam
  i2c_id: bsp_bus
  sensor_type: ov02c10
  xclk_pin: GPIO36
  xclk_freq: 24000000
  sensor_addr: 0x3C
  resolution: "800x480"
  pixel_format: RGB565
  framerate: 30

# Display (pas besoin de lambda pour la caméra!)
display:
  - platform: ili9xxx
    model: st7796
    id: camera_display
    data_rate: 80MHz

# Page LVGL
lvgl:
  pages:
    - id: camera_page
      bg_color: 0x000000

      on_load:
        - lambda: |-
            ESP_LOGI("camera", "📸 Page caméra OV02C10 chargée");

            // Configurer le canvas
            auto canvas = id(camera_canvas);
            if (canvas != nullptr) {
              lv_obj_set_size(canvas, 800, 480);
              lv_obj_set_pos(canvas, 0, 0);
              id(tab5_cam).configure_canvas(canvas);
              ESP_LOGI("camera", "✅ Canvas configuré: 800x480");
            }

            // Optionnel: démarrage automatique
            // if (id(tab5_cam).start_streaming()) {
            //   lv_label_set_text(id(status), "LIVE");
            //   lv_obj_set_style_text_color(id(status), lv_color_hex(0x00FF00), 0);
            // }

      widgets:
        # Canvas plein écran
        - canvas:
            id: camera_canvas
            width: 800
            height: 480
            x: 0
            y: 0
            bg_color: 0x000000

        # Bouton BACK
        - button:
            width: 100
            height: 45
            x: 680
            y: 10
            bg_color: 0x333333
            radius: 8
            on_click:
              then:
                - lambda: id(tab5_cam).stop_streaming();
                - lvgl.page.show: page_home
            widgets:
              - label:
                  text: "BACK"
                  text_color: 0xFFFFFF
                  text_font: nunito_20

        # Bouton START
        - button:
            width: 100
            height: 45
            x: 680
            y: 65
            bg_color: 0x00AA00
            radius: 8
            on_click:
              then:
                - lambda: |-
                    id(tab5_cam).start_streaming();
                    lv_label_set_text(id(status), "LIVE");
            widgets:
              - label:
                  text: "START"
                  text_color: 0xFFFFFF
                  text_font: nunito_20

        # Bouton STOP
        - button:
            width: 100
            height: 45
            x: 680
            y: 120
            bg_color: 0xCC0000
            radius: 8
            on_click:
              then:
                - lambda: |-
                    id(tab5_cam).stop_streaming();
                    lv_label_set_text(id(status), "STOP");
            widgets:
              - label:
                  text: "STOP"
                  text_color: 0xFFFFFF
                  text_font: nunito_20

        # Bouton INFO
        - button:
            width: 100
            height: 45
            x: 680
            y: 175
            bg_color: 0x4682B4
            radius: 8
            on_click:
              then:
                - lambda: |-
                    ESP_LOGI("camera", "OV02C10 800x480 RGB565 BGGR");
            widgets:
              - label:
                  text: "INFO"
                  text_color: 0xFFFFFF
                  text_font: nunito_20

        # Status
        - label:
            id: status
            text: "READY"
            x: 730
            y: 450
            text_color: 0xFF8800
            text_font: nunito_24
```

---

## 🎨 Personnalisation

### Changer les Couleurs des Boutons

```yaml
# Bouton vert → bleu
- button:
    bg_color: 0x0066CC   # Au lieu de 0x00AA00
```

### Repositionner les Boutons

```yaml
# Boutons en bas au lieu de haut à droite
- button:
    x: 10
    y: 420    # En bas
```

### Ajouter un Label de Résolution

```yaml
- label:
    text: !lambda |-
      static char buf[30];
      snprintf(buf, sizeof(buf), "%dx%d",
        id(tab5_cam).get_image_width(),
        id(tab5_cam).get_image_height());
      return buf;
    x: 10
    y: 10
    text_color: 0xFFFFFF
    text_font: nunito_20
```

---

## 📚 Références

- [CUSTOM_FORMATS_OV02C10.md](CUSTOM_FORMATS_OV02C10.md) - Formats custom OV02C10
- [CUSTOM_FORMATS_OV5647.md](CUSTOM_FORMATS_OV5647.md) - Formats custom OV5647
- [CUSTOM_FORMATS_SC202CS.md](CUSTOM_FORMATS_SC202CS.md) - Format custom SC202CS VGA
- [WATCHDOG_TIMEOUT_FIX.md](WATCHDOG_TIMEOUT_FIX.md) - Fix du timeout watchdog

---

**Date:** 2025-01-09
**Sensors supportés:** OV02C10, OV5647, SC202CS
**Interface:** Simplifiée (4 boutons)
**Status:** Production ready ✅
