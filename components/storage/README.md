# Storage Component - SD Card Images & LVGL Decoders

**Composant unifié** pour charger des images/vidéos depuis la carte SD avec support des décodeurs LVGL avancés intégrés.

## 🎯 Avantages

- ✅ **Un seul composant** pour tout (pas besoin de lvgl_advanced_features séparé!)
- ✅ **Économise la PSRAM** - tout est sur la carte SD
- ✅ **Zéro fuite mémoire** - libération PSRAM garantie
- ✅ **Support complet** - JPEG, GIF, PNG, BMP, SVG, Lottie

## 🚀 Installation

```yaml
external_components:
  - source: github://youkorr/test2_esp_video_esphome
    components:
      - sd_mmc_card
      - storage
```

## 📖 Configuration

### Configuration Simple (JPEG/GIF uniquement)

```yaml
sd_mmc_card:
  id: sd_card
  # ... pins configuration ...

storage:
  sd_images:
    - id: my_photo
      file_path: "/photos/photo.jpg"

    - id: my_animation
      file_path: "/gifs/logo.gif"
```

**C'est tout!** JPEG et GIF sont décodés nativement par le composant storage.

### Configuration Avancée (PNG, SVG, Lottie)

```yaml
storage:
  # Active les décodeurs LVGL pour formats avancés
  decoders:
    # Formats d'images (LVGL v8+v9)
    libpng: true          # PNG
    libjpeg_turbo: true   # JPEG optimisé (3-4x plus rapide)
    gif: true             # GIF animé
    bmp: true             # BMP

    # Graphiques vectoriels (LVGL v9 uniquement)
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

  sd_images:
    - id: photo_jpeg
      file_path: "/photos/photo.jpg"

    - id: animated_gif
      file_path: "/gifs/logo.gif"
```

**Note**: PNG/BMP/SVG/Lottie seront chargés via LVGL img widget, pas via storage component.

## 📝 Utilisation

### JPEG & GIF - Via Storage Component

```yaml
# Charger l'image JPEG
storage:
  sd_images:
    - id: photo
      file_path: "/photos/logo.jpg"

lvgl:
  displays:
    - my_display
  widgets:
    - image:
        src: photo  # Utilise storage
        x: 0
        y: 0

# GIF animé
storage:
  sd_images:
    - id: anim
      file_path: "/gifs/logo.gif"

lvgl:
  widgets:
    - image:
        src: anim  # Animation automatique
        x: 100
        y: 100
```

### PNG, BMP - Via LVGL Directement

```yaml
storage:
  decoders:
    libpng: true  # Active le décodeur PNG
    bmp: true     # Active le décodeur BMP

lvgl:
  widgets:
    # PNG décodé par LVGL directement
    - image:
        src: "S:/images/icon.png"  # S: = carte SD
        x: 0
        y: 0

    # BMP décodé par LVGL directement
    - image:
        src: "S:/images/background.bmp"
        x: 0
        y: 0
```

### SVG & Lottie - LVGL v9 Uniquement

```yaml
storage:
  decoders:
    thorvg:
      internal: true
    svg: true
    lottie: true

lvgl:
  widgets:
    # SVG vectoriel
    - image:
        src: "S:/vectors/icon.svg"
        x: 0
        y: 0
        width: 64
        height: 64

    # Animation Lottie
    - lottie:
        src: "S:/animations/loading.json"
        x: 100
        y: 100
        auto_start: true
```

## 🎬 Exemples Complets

### Galerie Photos Mixte (JPEG + PNG)

```yaml
storage:
  decoders:
    libpng: true  # Active PNG

  sd_images:
    # Images JPEG via storage
    - id: photo1
      file_path: "/photos/photo1.jpg"
    - id: photo2
      file_path: "/photos/photo2.jpg"

lvgl:
  widgets:
    # JPEG via storage
    - image:
        id: current_photo
        src: photo1
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
            // PNG via LVGL directement
            lv_img_set_src(id(current_photo), "S:/photos/photo3.png");
          }
```

### Slideshow avec Déchargement Mémoire

```yaml
storage:
  sd_images:
    - id: slideshow_image
      file_path: "/photos/photo1.jpg"

interval:
  - interval: 5s
    then:
      - lambda: |-
          static int photo_index = 0;

          // IMPORTANT: Décharger l'image précédente
          id(slideshow_image).unload_image();

          // Charger la nouvelle
          std::string path = "/photos/" + std::to_string(photo_index++) + ".jpg";
          id(slideshow_image).load_image_from_path(path);

          if (photo_index > 100) photo_index = 0;
```

## 📊 Matrice de Compatibilité

| Format | Storage Décode | LVGL Décode | Configuration Requise |
|--------|----------------|-------------|----------------------|
| **JPEG** | ✅ Built-in | ✅ LibJPEG Turbo | Aucune (ou `decoders: {libjpeg_turbo: true}` pour 3x faster) |
| **GIF** | ✅ Built-in | ✅ LVGL GIF | Aucune (ou `decoders: {gif: true}` pour LVGL) |
| **PNG** | ❌ | ✅ LibPNG | `decoders: {libpng: true}` |
| **BMP** | ❌ | ✅ Native | `decoders: {bmp: true}` |
| **SVG** | ❌ | ✅ ThorVG (v9) | `decoders: {thorvg: {internal: true}, svg: true}` |
| **Lottie** | ❌ | ✅ ThorVG (v9) | `decoders: {thorvg: {internal: true}, lottie: true}` |

## ⚡ Optimisations Performance

### Pour ESP32-P4 / ESP32-S3

```yaml
storage:
  decoders:
    # Optimisations ARM NEON
    draw_sw_complex: true
    draw_sw_asm: neon

    # Caches (PSRAM disponible)
    shadow_cache_size: 512  # Ko
    img_cache_size: 512     # Ko
```

**Gains**:
- NEON: 2-3x plus rapide pour le rendu
- Caches: Réduction latence d'affichage

### Recommandations Mémoire

| Plateforme | shadow_cache_size | img_cache_size |
|------------|------------------|----------------|
| ESP32-S3 (8MB PSRAM) | 256 Ko | 256 Ko |
| ESP32-P4 (16MB PSRAM) | 512 Ko | 512 Ko |

## 🔍 Détection Automatique de Format

Le composant détecte automatiquement le format:

```cpp
// Charge n'importe quel format supporté
id(my_image).load_image_from_path("/images/unknown_format.???");

// Logs affichent:
// [I][storage] Detected file type: PNG
// [E][storage] PNG decoding not implemented in storage
// [I][storage] Use LVGL img widget: src: "S:/images/unknown_format.png"
```

## ⚠️ Notes Importantes

### PNG/BMP/SVG/Lottie

Ces formats **ne sont PAS décodés** par le composant storage. À la place:

1. **Activez le décodeur** dans `decoders:`
2. **Utilisez LVGL img widget** directement:
   ```yaml
   lvgl:
     widgets:
       - image:
           src: "S:/path/to/file.png"  # Pas via storage!
   ```

### Gestion Mémoire PSRAM

**Toujours décharger** les images inutilisées:

```cpp
// ✅ BON
id(photo1).load_image_from_path("/photo1.jpg");
// ... utiliser ...
id(photo1).unload_image();  // Libère PSRAM

// ❌ MAUVAIS - Fuite mémoire
id(photo1).load_image_from_path("/photo1.jpg");
id(photo2).load_image_from_path("/photo2.jpg");
// photo1 toujours en mémoire!
```

### LVGL v9 Features

SVG, Lottie, Barcode requièrent **LVGL v9**:
- ESPHome avec LVGL v8 → ⚠️ Warning logs
- ESPHome avec LVGL v9 → ✅ Tout fonctionne

## 📚 Documentation Complémentaire

- [FIXES_SD_VIDEO_LOSS.md](../../FIXES_SD_VIDEO_LOSS.md) - Optimisations carte SD
- [FIXES_STORAGE_MEMORY_LEAK.md](../../FIXES_STORAGE_MEMORY_LEAK.md) - Corrections fuites PSRAM
- [INTEGRATION_SD_LVGL_STORAGE.md](../../INTEGRATION_SD_LVGL_STORAGE.md) - Guide d'intégration complet

## 🎉 Résumé

**Un seul composant, toutes les fonctionnalités!**

```yaml
# Configuration ultra-simple
storage:
  decoders:
    libpng: true    # Active PNG
    svg: true       # Active SVG (v9)
    lottie: true    # Active Lottie (v9)

  sd_images:
    - id: my_jpeg
      file_path: "/photo.jpg"  # Décodé par storage
```

Tout ce dont vous avez besoin pour charger images et vidéos depuis la carte SD! 🚀
