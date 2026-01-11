# LVGL Advanced Features for ESPHome

**Compatible avec LVGL v8 et v9** 🎉

Ce composant active les fonctionnalités avancées de LVGL qui sont désactivées par défaut dans ESPHome, notamment le support de formats d'images avancés (PNG, JPEG Turbo, GIF, BMP), les widgets (QR Code, Barcode), les optimisations de performance, et les fonctionnalités de graphiques vectoriels (ThorVG, SVG, Lottie) pour LVGL v9.

## 📋 Tableau de Compatibilité

| Fonctionnalité | LVGL v8 | LVGL v9 | Notes |
|----------------|---------|---------|-------|
| **Formats d'Images** | | | |
| LibPNG | ✅ | ✅ | Support PNG haute qualité |
| LibJPEG Turbo | ✅ | ✅ | JPEG optimisé (3-4x plus rapide) |
| GIF | ✅ | ✅ | Animations GIF |
| BMP | ✅ | ✅ | Images bitmap |
| **Graphiques Vectoriels** | | | |
| ThorVG Internal | ❌ | ✅ | Moteur vectoriel intégré |
| ThorVG External | ❌ | ✅ | Bibliothèque ThorVG externe |
| SVG | ❌ | ✅ | Images vectorielles SVG |
| Lottie | ❌ | ✅ | Animations Lottie |
| **Widgets** | | | |
| QR Code | ✅ | ✅ | Génération de QR codes |
| Barcode | ❌ | ✅ | Code-barres (v9) |
| **Polices** | | | |
| FreeType | ✅ | ✅ | Rendu avancé de polices |
| **Performance** | | | |
| Draw SW Complex | ✅ | ✅ | Optimisations de rendu |
| ASM NEON/Helium | ✅ | ✅ | Optimisations ARM |
| Shadow Cache | ✅ | ✅ | Cache d'ombres |
| Image Cache | ✅ | ✅ | Cache d'images |

Legend:
- ✅ = Compatible
- ❌ = Non disponible

## 🚀 Installation

Ajoutez le composant à votre configuration ESPHome:

```yaml
external_components:
  - source: github://youkorr/test2_esp_video_esphome
    components: [ lvgl_advanced_features ]
```

## 📖 Configuration

### Configuration Minimale (LVGL v8 Compatible)

```yaml
lvgl_advanced_features:
  # Formats d'images (v8+v9)
  libpng: true          # Support PNG
  libjpeg_turbo: true   # Support JPEG optimisé
  gif: true             # Support GIF animé
  bmp: true             # Support BMP

  # Widgets (v8+v9)
  qrcode: true          # Widget QR Code

  # Polices (v8+v9)
  freetype: true        # Rendu avancé de polices

  # Performance (v8+v9)
  draw_sw_complex: true # Optimisations de rendu
```

### Configuration Complète (LVGL v9)

```yaml
lvgl_advanced_features:
  # Graphiques vectoriels (v9 uniquement)
  thorvg:
    internal: true      # ThorVG intégré
    external: false     # ThorVG externe
  svg: true             # Support SVG
  lottie: true          # Animations Lottie

  # Formats d'images (v8+v9)
  libpng: true
  libjpeg_turbo: true
  gif: true
  bmp: true

  # Widgets
  qrcode: true          # QR Code (v8+v9)
  barcode: true         # Code-barres (v9 uniquement)

  # Polices
  freetype: true        # FreeType (v8+v9)

  # Performance
  draw_sw_complex: true
  draw_sw_asm: neon     # Options: none, neon, helium
  shadow_cache_size: 256
  img_cache_size: 256
```

## 📊 Exemples d'Utilisation

### QR Code (v8+v9 Compatible)

```yaml
lvgl:
  displays:
    - my_display
  pages:
    - id: main_page
      widgets:
        - qrcode:
            x: 50
            y: 50
            size: 200
            dark_color: 0x000000
            light_color: 0xFFFFFF
            data: "https://esphome.io"
```

### Image PNG (v8+v9 Compatible)

```yaml
# Dans votre configuration LVGL
image:
  - id: my_logo
    file: "logo.png"  # PNG sera décodé avec LibPNG

lvgl:
  widgets:
    - image:
        src: my_logo
        x: 0
        y: 0
```

### Animation GIF (v8+v9 Compatible)

```yaml
image:
  - id: loading_animation
    file: "loading.gif"  # GIF animé

lvgl:
  widgets:
    - image:
        src: loading_animation
        x: 100
        y: 100
```

### SVG (v9 Uniquement)

```yaml
# Requiert LVGL v9 + ThorVG
image:
  - id: vector_icon
    file: "icon.svg"  # Image vectorielle

lvgl:
  widgets:
    - image:
        src: vector_icon
        x: 0
        y: 0
```

### Lottie Animation (v9 Uniquement)

```yaml
# Requiert LVGL v9 + ThorVG
lvgl:
  widgets:
    - lottie:
        src: "animation.json"
        x: 50
        y: 50
        auto_start: true
```

## 🔧 Logs de Débogage

Lors du démarrage, le composant affiche les fonctionnalités activées:

### Avec LVGL v9:
```
[I][lvgl_advanced_features] ========================================
[I][lvgl_advanced_features] LVGL Advanced Features - Setup
[I][lvgl_advanced_features] ========================================
[I][lvgl_advanced_features] LVGL Version: 9.0.0
[I][lvgl_advanced_features] ✅ LVGL v9+ detected - All features available
[I][lvgl_advanced_features] ----------------------------------------
[I][lvgl_advanced_features] Feature Status:
[I][lvgl_advanced_features] ----------------------------------------
[I][lvgl_advanced_features]   ✓ ThorVG Internal: ENABLED (v9)
[I][lvgl_advanced_features]   ✓ SVG Support: ENABLED (v9)
[I][lvgl_advanced_features]   ✓ Lottie Support: ENABLED (v9)
[I][lvgl_advanced_features]   ✓ LibPNG: ENABLED (v8+v9)
[I][lvgl_advanced_features]   ✓ LibJPEG Turbo: ENABLED (v8+v9)
[I][lvgl_advanced_features]   ✓ GIF: ENABLED (v8+v9)
[I][lvgl_advanced_features]   ✓ QR Code: ENABLED (v8+v9)
[I][lvgl_advanced_features]   ✓ Barcode Widget: ENABLED (v9)
[I][lvgl_advanced_features] ========================================
[I][lvgl_advanced_features] Setup complete!
```

### Avec LVGL v8:
```
[I][lvgl_advanced_features] ========================================
[I][lvgl_advanced_features] LVGL Advanced Features - Setup
[I][lvgl_advanced_features] ========================================
[I][lvgl_advanced_features] LVGL Version: 8.3.11
[W][lvgl_advanced_features] ⚠️  LVGL v8 detected - Limited features available
[W][lvgl_advanced_features]    v8 compatible: PNG, JPEG, GIF, BMP, QRCode, FreeType
[W][lvgl_advanced_features]    v9 only: ThorVG, SVG, Lottie, Barcode, FFmpeg
[I][lvgl_advanced_features] ----------------------------------------
[I][lvgl_advanced_features] Feature Status:
[I][lvgl_advanced_features] ----------------------------------------
[W][lvgl_advanced_features]   ✗ ThorVG Internal: REQUESTED but requires LVGL v9
[W][lvgl_advanced_features]   ✗ SVG Support: REQUESTED but requires LVGL v9 + ThorVG
[I][lvgl_advanced_features]   ✓ LibPNG: ENABLED (v8+v9)
[I][lvgl_advanced_features]   ✓ LibJPEG Turbo: ENABLED (v8+v9)
[I][lvgl_advanced_features]   ✓ GIF: ENABLED (v8+v9)
[I][lvgl_advanced_features]   ✓ QR Code: ENABLED (v8+v9)
[I][lvgl_advanced_features] ========================================
[I][lvgl_advanced_features] Setup complete!
```

## ⚡ Optimisations de Performance

### ASM Optimizations

```yaml
lvgl_advanced_features:
  draw_sw_asm: neon  # ARM NEON (ESP32-P4, ESP32-S3)
  # draw_sw_asm: helium  # ARM Helium (Cortex-M v8.1-M)
```

**Gains de performance**:
- NEON: 2-3x plus rapide pour le rendu
- Helium: 3-4x plus rapide (sur MCU compatibles)

### Cache Configuration

```yaml
lvgl_advanced_features:
  shadow_cache_size: 256  # Ko, cache des ombres
  img_cache_size: 256     # Ko, cache des images
```

**Recommandations**:
- ESP32-S3 (8MB PSRAM): 256-512 Ko par cache
- ESP32-P4 (16MB PSRAM): 512-1024 Ko par cache

## 🐛 Troubleshooting

### "Feature REQUESTED but requires LVGL v9"

**Cause**: Vous avez activé une fonctionnalité v9 (ThorVG, SVG, Lottie, Barcode) mais utilisez LVGL v8.

**Solution**:
1. Mettez à jour ESPHome vers une version avec LVGL v9
2. OU désactivez les fonctionnalités v9 dans votre configuration

### "ThorVG Internal: REQUESTED but not compiled"

**Cause**: ThorVG nécessite une compilation spécifique de LVGL.

**Solution**: Assurez-vous que LVGL est compilé avec ThorVG activé (nécessite LVGL v9).

### Images PNG/JPEG/GIF ne s'affichent pas

**Vérifications**:
1. Les build flags sont bien appliqués (vérifiez les logs de compilation)
2. Les bibliothèques sont disponibles sur votre plateforme
3. Les fichiers d'images sont au bon format

## 📚 Ressources

- [LVGL Documentation](https://docs.lvgl.io/)
- [ThorVG Documentation](https://www.thorvg.org/)
- [ESPHome LVGL Documentation](https://esphome.io/components/display/lvgl.html)

## 🤝 Contribution

Ce composant est compatible avec LVGL v8 et v9. Si vous rencontrez des problèmes ou avez des suggestions d'amélioration, n'hésitez pas à ouvrir une issue.

## 📄 License

Ce composant suit la même licence qu'ESPHome (MIT).

---

**Note**: Les fonctionnalités v9 (ThorVG, SVG, Lottie, Barcode) ne fonctionneront que si votre version d'ESPHome inclut LVGL v9. Les autres fonctionnalités (PNG, JPEG, GIF, QR Code, FreeType) sont compatibles avec LVGL v8 et v9.
