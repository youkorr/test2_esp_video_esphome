# Intégration SD Card + LVGL Advanced Features + Storage

## 🎯 Vue d'Ensemble

Ce repository fournit **3 composants complémentaires** qui travaillent ensemble pour charger et afficher des images/animations depuis une carte SD sur ESP32-P4/ESP32-S3:

### 1. **sd_mmc_card** - Accès Rapide à la Carte SD
- ✅ Lecture/écriture optimisée pour fichiers vidéo 300+ Mo
- ✅ Support SDMMC haute vitesse (UHS-I U3)
- ✅ `write_file_video()` - Écriture avec fflush + fsync optionnel
- ✅ `read_file_video()` - Lecture complète de gros fichiers
- ✅ `read_file_stream()` - Streaming pour fichiers >500 Mo

### 2. **lvgl_advanced_features** - Décodeurs d'Images LVGL
- ✅ **LVGL v8**: PNG, JPEG, GIF, BMP, QRCode, FreeType
- ✅ **LVGL v9**: Tout v8 + ThorVG, SVG, Lottie, Barcode
- ✅ Détection automatique de version
- ✅ Optimisations de performance (NEON, caches)

### 3. **storage** - Composant d'Images ESPHome
- ✅ Charge JPEG et GIF depuis SD (décodage intégré)
- ✅ Support animations GIF (60+ frames)
- ✅ Gestion mémoire PSRAM sans fuites
- ✅ Détection automatique de format (JPEG, GIF, PNG, BMP, SVG, Lottie)

## 📋 Matrice de Compatibilité

| Format | sd_mmc_card | storage (décodage) | LVGL (décodage) | Recommandation |
|--------|-------------|-------------------|-----------------|----------------|
| **JPEG** | ✅ Lecture | ✅ JPEGDEC | ✅ LibJPEG Turbo | `storage` pour ESPHome, `LVGL img` pour LVGL |
| **GIF** | ✅ Lecture | ✅ Custom LZW | ✅ Native | `storage` pour animations complexes |
| **PNG** | ✅ Lecture | ⚠️ Via LVGL | ✅ LibPNG | **Utiliser LVGL img widget directement** |
| **BMP** | ✅ Lecture | ⚠️ Via LVGL | ✅ Native | **Utiliser LVGL img widget directement** |
| **SVG** | ✅ Lecture | ⚠️ Via LVGL | ✅ ThorVG (v9) | **Utiliser LVGL img widget directement** |
| **Lottie** | ✅ Lecture | ⚠️ Via LVGL | ✅ ThorVG (v9) | **Utiliser LVGL lottie widget directement** |

## 🚀 Installation

### Configuration ESPHome Complète

```yaml
external_components:
  - source: github://youkorr/test2_esp_video_esphome
    components:
      - sd_mmc_card
      - storage
      - lvgl_advanced_features

# 1. Carte SD (obligatoire pour tous)
sd_mmc_card:
  id: sd_card
  cs_pin: GPIO10
  clk_pin: GPIO12
  cmd_pin: GPIO11
  data_pins:
    - GPIO13
    - GPIO14
    - GPIO15
    - GPIO16

# 2. LVGL Advanced Features (active les décodeurs)
lvgl_advanced_features:
  # Formats d'images (v8+v9)
  libpng: true          # PNG
  libjpeg_turbo: true   # JPEG optimisé
  gif: true             # GIF
  bmp: true             # BMP

  # Graphiques vectoriels (v9 uniquement)
  thorvg:
    internal: true      # ThorVG intégré
  svg: true             # SVG
  lottie: true          # Animations Lottie

  # Widgets
  qrcode: true          # QR Code
  barcode: true         # Code-barres (v9)

  # Performance
  draw_sw_complex: true
  draw_sw_asm: neon     # ARM NEON pour ESP32-P4/S3
  shadow_cache_size: 256
  img_cache_size: 256

# 3. Storage (optionnel - pour JPEG/GIF avec ESPHome)
storage:
  - id: my_jpeg_image
    sd_mmc_id: sd_card
    file_path: "/photos/logo.jpg"
```

## 📖 Utilisation par Format

### 🖼️ JPEG - Deux Méthodes

#### Méthode 1: Composant Storage (Recommandé pour ESPHome Display)

```yaml
storage:
  - id: photo_jpeg
    sd_mmc_id: sd_card
    file_path: "/photos/photo1.jpg"

display:
  - platform: ...
    lambda: |-
      // Afficher l'image JPEG
      id(photo_jpeg).draw(0, 0, it);
```

#### Méthode 2: LVGL Image Widget (Recommandé pour LVGL)

```yaml
lvgl:
  displays:
    - my_display
  pages:
    - id: main_page
      widgets:
        - image:
            src: "S:/photos/photo1.jpg"  # S: = carte SD
            x: 0
            y: 0
```

### 🎞️ GIF Animé - Composant Storage

```yaml
storage:
  - id: animated_logo
    sd_mmc_id: sd_card
    file_path: "/gifs/logo.gif"

lvgl:
  widgets:
    - image:
        src: animated_logo
        x: 100
        y: 100

# L'animation se joue automatiquement!
# Contrôle manuel possible:
script:
  - id: next_frame
    then:
      - lambda: |-
          id(animated_logo).next_frame();

  - id: set_frame
    parameters:
      frame: int
    then:
      - lambda: |-
          id(animated_logo).set_frame(frame);
```

### 🎨 PNG - LVGL Image Widget Uniquement

```yaml
# PNG est décodé par LVGL directement (LibPNG activé par lvgl_advanced_features)
lvgl:
  widgets:
    - image:
        src: "S:/images/icon.png"  # S: = carte SD
        x: 0
        y: 0
```

**Note**: PNG n'est PAS décodé par le composant `storage` - utilisez LVGL img widget directement.

### 🖌️ BMP - LVGL Image Widget Uniquement

```yaml
lvgl:
  widgets:
    - image:
        src: "S:/images/background.bmp"
        x: 0
        y: 0
```

### 🎯 SVG - LVGL Image Widget (LVGL v9 uniquement)

```yaml
# Requiert LVGL v9 + ThorVG
lvgl:
  widgets:
    - image:
        src: "S:/vectors/icon.svg"
        x: 0
        y: 0
        # SVG est redimensionnable sans perte de qualité!
        width: 200
        height: 200
```

**Prérequis**:
- ESPHome avec LVGL v9 (en développement)
- `lvgl_advanced_features` avec `svg: true` et `thorvg: {internal: true}`

### 🎭 Lottie - LVGL Lottie Widget (LVGL v9 uniquement)

```yaml
# Requiert LVGL v9 + ThorVG
lvgl:
  widgets:
    - lottie:
        src: "S:/animations/loading.json"
        x: 50
        y: 50
        auto_start: true
        loop: true
```

**Prérequis**:
- ESPHome avec LVGL v9 (en développement)
- `lvgl_advanced_features` avec `lottie: true` et `thorvg: {internal: true}`

### 📱 QR Code - LVGL QR Code Widget

```yaml
lvgl:
  widgets:
    - qrcode:
        x: 50
        y: 50
        size: 200
        dark_color: 0x000000
        light_color: 0xFFFFFF
        data: "https://esphome.io"
```

## 🎬 Cas d'Usage Complets

### Cas 1: Galerie de Photos JPEG/PNG

```yaml
storage:
  # Images JPEG via storage
  - id: photo1
    sd_mmc_id: sd_card
    file_path: "/photos/photo1.jpg"
  - id: photo2
    sd_mmc_id: sd_card
    file_path: "/photos/photo2.jpg"

lvgl:
  widgets:
    - image:
        id: current_photo
        src: photo1
        x: 0
        y: 0

    # PNG via LVGL directement
    - image:
        src: "S:/photos/photo3.png"
        x: 0
        y: 0

button:
  - platform: gpio
    name: "Next Photo"
    on_press:
      - lambda: |-
          static int index = 0;
          index = (index + 1) % 3;

          if (index == 0) {
            id(photo1).load_image_from_path("/photos/photo1.jpg");
            lv_img_set_src(id(current_photo), &id(photo1));
          } else if (index == 1) {
            id(photo2).load_image_from_path("/photos/photo2.jpg");
            lv_img_set_src(id(current_photo), &id(photo2));
          } else {
            lv_img_set_src(id(current_photo), "S:/photos/photo3.png");
          }
```

### Cas 2: Logo Animé GIF + Icons SVG (v9)

```yaml
storage:
  - id: animated_logo
    sd_mmc_id: sd_card
    file_path: "/branding/logo.gif"

lvgl:
  widgets:
    # Logo GIF animé
    - image:
        src: animated_logo
        x: 100
        y: 50

    # Icons SVG vectoriels (v9 uniquement)
    - image:
        src: "S:/icons/wifi.svg"
        x: 10
        y: 10
        width: 32
        height: 32

    - image:
        src: "S:/icons/battery.svg"
        x: 50
        y: 10
        width: 32
        height: 32
```

### Cas 3: Slideshow Automatique (Optimisé PSRAM)

```yaml
storage:
  - id: slideshow_image
    sd_mmc_id: sd_card

interval:
  - interval: 5s
    then:
      - lambda: |-
          static int photo_index = 0;

          // IMPORTANT: Décharger l'image précédente pour libérer la PSRAM
          id(slideshow_image).unload_image();

          // Charger la nouvelle image
          std::string path = "/photos/" + std::to_string(photo_index++) + ".jpg";
          id(slideshow_image).load_image_from_path(path);

          if (photo_index > 100) photo_index = 0;

          ESP_LOGI("slideshow", "Photo %d loaded", photo_index);
```

### Cas 4: Écran de Chargement Lottie (v9)

```yaml
lvgl:
  pages:
    - id: loading_page
      widgets:
        - lottie:
            src: "S:/animations/loading.json"
            x: 120
            y: 160
            auto_start: true
            loop: true

    - id: main_page
      widgets:
        - image:
            src: "S:/images/home.png"
            x: 0
            y: 0

on_boot:
  then:
    # Afficher écran de chargement
    - lvgl.page.show: loading_page

    # Initialiser l'application
    - delay: 3s

    # Passer à l'écran principal
    - lvgl.page.show: main_page
```

## 📊 Comparaison des Approches

### JPEG/GIF: Storage vs LVGL

| Aspect | Storage Component | LVGL IMG Widget |
|--------|------------------|-----------------|
| **Décodage** | JPEGDEC / Custom LZW | LibJPEG Turbo / LVGL GIF |
| **Animations GIF** | ✅ Full control (60+ frames) | ✅ Automatique |
| **ESPHome Display** | ✅ Compatible | ❌ Non compatible |
| **LVGL Display** | ✅ Compatible | ✅ Compatible |
| **Gestion Mémoire** | Manuel (`unload_image()`) | Automatique (cache LVGL) |
| **Performance** | Bon (JPEGDEC rapide) | Excellent (LibJPEG Turbo 3-4x faster) |

**Recommandation**:
- **Storage**: Pour ESPHome Display ou animations GIF complexes
- **LVGL IMG**: Pour LVGL Display avec meilleure performance

### PNG/BMP/SVG/Lottie: LVGL Uniquement

Ces formats sont **toujours** décodés par LVGL:

| Format | Décodeur LVGL | Disponibilité |
|--------|--------------|---------------|
| PNG | LibPNG | v8 + v9 |
| BMP | Native | v8 + v9 |
| SVG | ThorVG | v9 uniquement |
| Lottie | ThorVG | v9 uniquement |

**Usage**:
```yaml
lvgl:
  widgets:
    - image:
        src: "S:/path/to/file.png"  # ou .bmp, .svg
```

## 🧪 Test de Compatibilité

### Vérifier les Décodeurs Activés

Les logs au démarrage montrent quels décodeurs sont disponibles:

```
[I][lvgl_advanced_features] ========================================
[I][lvgl_advanced_features] LVGL Version: 8.3.11
[I][lvgl_advanced_features] ----------------------------------------
[I][lvgl_advanced_features]   ✓ LibPNG: ENABLED (v8+v9)
[I][lvgl_advanced_features]   ✓ LibJPEG Turbo: ENABLED (v8+v9)
[I][lvgl_advanced_features]   ✓ GIF: ENABLED (v8+v9)
[I][lvgl_advanced_features]   ✓ BMP: ENABLED (v8+v9)
[I][lvgl_advanced_features]   ✓ QR Code: ENABLED (v8+v9)
[I][lvgl_advanced_features]   ✗ SVG Support: REQUESTED but requires LVGL v9 + ThorVG
[I][lvgl_advanced_features] ========================================
```

### Tester Chaque Format

```yaml
button:
  - platform: gpio
    name: "Test JPEG"
    on_press:
      - lambda: |-
          id(test_image).load_image_from_path("/test/test.jpg");

  - platform: gpio
    name: "Test PNG"
    on_press:
      - lambda: |-
          lv_img_set_src(id(test_img_widget), "S:/test/test.png");

  - platform: gpio
    name: "Test GIF"
    on_press:
      - lambda: |-
          id(test_gif).load_image_from_path("/test/anim.gif");
```

## ⚠️ Limitations et Solutions

### PNG/BMP/SVG/Lottie dans Storage

**Limitation**: Le composant `storage` ne décode PAS ces formats - il détecte le type et affiche un message d'erreur instructif.

**Solution**: Utiliser LVGL img/lottie widgets directement:

```yaml
# ❌ NE FONCTIONNE PAS
storage:
  - id: my_png
    file_path: "/images/icon.png"  # Erreur: PNG not decoded

# ✅ FONCTIONNE
lvgl:
  widgets:
    - image:
        src: "S:/images/icon.png"  # LVGL décode directement
```

### SVG/Lottie Requiert LVGL v9

**Limitation**: ThorVG (requis pour SVG/Lottie) n'est disponible qu'en LVGL v9.

**Vérification Version**:
```yaml
sensor:
  - platform: template
    name: "LVGL Version"
    lambda: |-
      return LVGL_VERSION_MAJOR;
    update_interval: never
```

**Solution**:
- Attendre la release ESPHome avec LVGL v9
- Ou compiler ESPHome custom avec LVGL v9 (voir PRs #12320, #12312)

### Mémoire PSRAM Limitée

**Problème**: Trop d'images chargées saturent la PSRAM.

**Solution**: Décharger les images inutilisées:

```yaml
script:
  - id: load_next_photo
    then:
      - lambda: |-
          // Décharger l'ancienne image
          id(current_photo).unload_image();

          // Charger la nouvelle
          id(current_photo).load_image_from_path("/photos/new.jpg");
```

**Surveillance PSRAM**:
```yaml
sensor:
  - platform: template
    name: "Free PSRAM"
    lambda: |-
      return heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024.0;
    unit_of_measurement: "KB"
    update_interval: 5s
```

## 📚 Documentation Complémentaire

- [FIXES_SD_VIDEO_LOSS.md](FIXES_SD_VIDEO_LOSS.md) - Optimisations carte SD
- [FIXES_STORAGE_MEMORY_LEAK.md](FIXES_STORAGE_MEMORY_LEAK.md) - Corrections fuites PSRAM
- [components/lvgl_advanced_features/README.md](components/lvgl_advanced_features/README.md) - Guide LVGL features

## 🎉 Résumé

| Besoin | Composants Requis | Configuration |
|--------|------------------|---------------|
| **Images JPEG/GIF sur ESPHome Display** | sd_mmc_card + storage | `storage:` avec file_path |
| **Images JPEG/PNG/BMP sur LVGL** | sd_mmc_card + lvgl_advanced_features | `lvgl: image: src: "S:/..."` |
| **SVG/Lottie sur LVGL v9** | sd_mmc_card + lvgl_advanced_features | `lvgl: image/lottie: src: "S:/..."` |
| **Vidéo 300+ Mo** | sd_mmc_card | `write_file_video()`, `read_file_video()` |

**Les 3 composants travaillent ensemble** pour offrir une solution complète de gestion d'images/vidéos depuis la carte SD! 🚀
