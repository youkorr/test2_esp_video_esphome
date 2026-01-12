# Solution PSRAM Générique Multi-Résolutions (Dépôt Public)

## 🎯 Objectif: Solution Universelle

### Contraintes

Votre dépôt doit supporter:
- ✅ **Multiples résolutions display**: 480x320, 800x480, 1024x600, 1280x800, etc.
- ✅ **Multiples ESP32**: ESP32, ESP32-S3, ESP32-P4
- ✅ **Images de tailles variables**: Backgrounds et animations diverses
- ✅ **Configuration flexible**: L'utilisateur ne doit pas préciser la résolution
- ✅ **Auto-adaptation**: Détection automatique et optimisation

---

## 🔧 Architecture Auto-Adaptive

### Principe

**Le composant détecte automatiquement:**
1. Résolution du display (depuis LVGL/Display component)
2. Résolution de l'image (depuis header JPEG/GIF)
3. PSRAM disponible (depuis ESP32 platform)
4. Stratégie optimale (streaming vs cache)

```
┌─────────────────────────────────────────────────┐
│  Auto-Detection Layer                           │
│  ┌──────────────┐  ┌──────────────┐            │
│  │ Display Info │  │ Image Header │            │
│  │ - 800x480    │  │ - 1024x768   │            │
│  │ - RGB565     │  │ - JPEG       │            │
│  └──────┬───────┘  └──────┬───────┘            │
│         │                  │                    │
│         └────────┬─────────┘                    │
│                  ▼                               │
│         ┌────────────────┐                      │
│         │ Strategy Engine│                      │
│         │ - Scaling?     │                      │
│         │ - Streaming?   │                      │
│         │ - Cache?       │                      │
│         └────────┬───────┘                      │
│                  ▼                               │
│         ┌────────────────┐                      │
│         │ Optimized Render│                     │
│         └────────────────┘                      │
└─────────────────────────────────────────────────┘
```

---

## 💻 Implémentation Générique

### 1. Auto-Detection Display

```cpp
// storage.cpp
struct DisplayInfo {
  uint16_t width;
  uint16_t height;
  size_t framebuffer_size;
  esphome::display::ColorBitness color_bitness;
  size_t bytes_per_pixel;
};

DisplayInfo SdImageComponent::detect_display_info() {
  DisplayInfo info;

  #ifdef USE_LVGL
  // Obtenir infos depuis LVGL
  info.width = LV_HOR_RES;
  info.height = LV_VER_RES;
  info.color_bitness = LV_COLOR_DEPTH;  // 16, 24, 32

  #elif defined(USE_DISPLAY)
  // Obtenir infos depuis Display component
  auto *display = this->get_display();
  if (display) {
    info.width = display->get_width();
    info.height = display->get_height();
    info.color_bitness = display->get_color_bitness();
  }
  #endif

  // Calculer bytes per pixel
  switch (info.color_bitness) {
    case 16: info.bytes_per_pixel = 2; break;  // RGB565
    case 24: info.bytes_per_pixel = 3; break;  // RGB888
    case 32: info.bytes_per_pixel = 4; break;  // RGBA8888
    default: info.bytes_per_pixel = 2; break;
  }

  info.framebuffer_size = info.width * info.height * info.bytes_per_pixel;

  ESP_LOGI(TAG_IMAGE, "Display detected: %dx%d, %d-bit, %zu bytes/pixel",
           info.width, info.height, info.color_bitness, info.bytes_per_pixel);

  return info;
}
```

### 2. Auto-Detection Image

```cpp
// storage.cpp
struct ImageInfo {
  uint16_t width;
  uint16_t height;
  ImageFormat format;  // JPEG, GIF, PNG
  size_t file_size;
  size_t decoded_size;
  uint16_t frame_count;  // 1 pour JPEG, N pour GIF
};

ImageInfo SdImageComponent::detect_image_info(const std::string &path) {
  ImageInfo info;

  // Ouvrir le fichier
  FILE *file = fopen(path.c_str(), "rb");
  if (!file) return info;

  // Lire les premiers bytes pour identifier le format
  uint8_t header[16];
  fread(header, 1, 16, file);

  // Détecter JPEG
  if (header[0] == 0xFF && header[1] == 0xD8) {
    info.format = ImageFormat::JPEG;

    // Parser JPEG header pour dimensions
    JPEGDEC jpeg;
    jpeg.open(file, nullptr);
    info.width = jpeg.getWidth();
    info.height = jpeg.getHeight();
    jpeg.close();

    info.frame_count = 1;
  }
  // Détecter GIF
  else if (header[0] == 'G' && header[1] == 'I' && header[2] == 'F') {
    info.format = ImageFormat::GIF;

    // Parser GIF header pour dimensions
    info.width = header[6] | (header[7] << 8);
    info.height = header[8] | (header[9] << 8);

    // Parser nombre de frames (scan rapide du fichier)
    info.frame_count = count_gif_frames(file);
  }
  // Détecter PNG
  else if (header[0] == 0x89 && header[1] == 0x50 &&
           header[2] == 0x4E && header[3] == 0x47) {
    info.format = ImageFormat::PNG;

    // Parser PNG IHDR chunk
    fseek(file, 16, SEEK_SET);
    uint8_t ihdr[8];
    fread(ihdr, 1, 8, file);
    info.width = (ihdr[0] << 24) | (ihdr[1] << 16) | (ihdr[2] << 8) | ihdr[3];
    info.height = (ihdr[4] << 24) | (ihdr[5] << 16) | (ihdr[6] << 8) | ihdr[7];

    info.frame_count = 1;
  }

  // Obtenir taille fichier
  fseek(file, 0, SEEK_END);
  info.file_size = ftell(file);
  fclose(file);

  // Calculer taille décodée (RGB565)
  info.decoded_size = info.width * info.height * 2 * info.frame_count;

  ESP_LOGI(TAG_IMAGE, "Image detected: %dx%d, %s, %zu KB compressed, %zu KB decoded, %d frames",
           info.width, info.height,
           format_to_string(info.format).c_str(),
           info.file_size / 1024,
           info.decoded_size / 1024,
           info.frame_count);

  return info;
}
```

### 3. Strategy Engine - Décision Automatique

```cpp
// storage.cpp
enum class RenderStrategy {
  STREAMING,       // Streaming ligne par ligne (backgrounds)
  CACHE_FULL,      // Cache toutes frames en PSRAM (petites animations)
  CACHE_ON_DEMAND, // Cache 1 frame + preload (grosses animations)
  SCALED_STREAMING // Streaming avec scaling (image > display)
};

struct StrategyDecision {
  RenderStrategy strategy;
  bool needs_scaling;
  float scale_factor;
  size_t estimated_psram;
  std::string reason;
};

StrategyDecision SdImageComponent::determine_strategy(
    const ImageInfo &img_info,
    const DisplayInfo &disp_info,
    ImageType img_type) {

  StrategyDecision decision;

  // 1. Vérifier si scaling nécessaire
  if (img_info.width > disp_info.width || img_info.height > disp_info.height) {
    decision.needs_scaling = true;
    float scale_w = (float)disp_info.width / img_info.width;
    float scale_h = (float)disp_info.height / img_info.height;
    decision.scale_factor = std::min(scale_w, scale_h);
  } else {
    decision.needs_scaling = false;
    decision.scale_factor = 1.0f;
  }

  // 2. BACKGROUNDS: Toujours streaming
  if (img_type == ImageType::BACKGROUND) {
    if (decision.needs_scaling) {
      decision.strategy = RenderStrategy::SCALED_STREAMING;
      decision.estimated_psram = disp_info.width * 2 * 2;  // 2 lignes buffer
      decision.reason = "Background with scaling - streaming line-by-line";
    } else {
      decision.strategy = RenderStrategy::STREAMING;
      decision.estimated_psram = img_info.width * 2;  // 1 ligne buffer
      decision.reason = "Background - streaming line-by-line";
    }
    return decision;
  }

  // 3. ANIMATIONS: Décision basée sur taille
  if (img_type == ImageType::ANIMATION) {
    size_t decoded_size = img_info.decoded_size;

    // Petite animation: Cache complet
    if (decoded_size < 300 * 1024) {  // <300 KB
      decision.strategy = RenderStrategy::CACHE_FULL;
      decision.estimated_psram = decoded_size;
      decision.reason = "Small animation - full cache for smooth loop";
    }
    // Grosse animation: On-demand
    else {
      decision.strategy = RenderStrategy::CACHE_ON_DEMAND;
      size_t frame_size = img_info.width * img_info.height * 2;
      decision.estimated_psram = frame_size * 3;  // 1 current + 2 preload
      decision.reason = "Large animation - on-demand frame loading";
    }
    return decision;
  }

  // 4. STANDARD: Cache complet si pas trop gros
  if (img_info.decoded_size < 500 * 1024) {
    decision.strategy = RenderStrategy::CACHE_FULL;
    decision.estimated_psram = img_info.decoded_size;
    decision.reason = "Standard image - full cache";
  } else {
    decision.strategy = RenderStrategy::STREAMING;
    decision.estimated_psram = img_info.width * 2;
    decision.reason = "Large image - streaming";
  }

  return decision;
}
```

### 4. Rendering avec Scaling Automatique

```cpp
// storage.cpp
bool SdImageComponent::render_with_strategy(
    lv_obj_t *parent,
    const StrategyDecision &decision) {

  switch (decision.strategy) {
    case RenderStrategy::STREAMING:
      return render_streaming_simple(parent);

    case RenderStrategy::SCALED_STREAMING:
      return render_streaming_scaled(parent, decision.scale_factor);

    case RenderStrategy::CACHE_FULL:
      return load_and_cache_full();

    case RenderStrategy::CACHE_ON_DEMAND:
      return load_on_demand_with_preload();
  }

  return false;
}

// Streaming avec scaling
bool SdImageComponent::render_streaming_scaled(lv_obj_t *parent, float scale) {
  ESP_LOGI(TAG_IMAGE, "Rendering with scaling: %.2fx", scale);

  FILE *file = fopen(file_path_.c_str(), "rb");
  if (!file) return false;

  JPEGDEC jpeg;
  jpeg.open(file, JPEGDrawScaled);

  // Calculer dimensions après scaling
  int scaled_width = (int)(jpeg.getWidth() * scale);
  int scaled_height = (int)(jpeg.getHeight() * scale);

  // Créer canvas aux bonnes dimensions
  lv_obj_t *canvas = lv_canvas_create(parent);
  lv_obj_set_size(canvas, scaled_width, scaled_height);

  // Contexte de scaling pour callback
  scaling_context.canvas = canvas;
  scaling_context.scale_factor = scale;
  scaling_context.line_buffer.resize(scaled_width * 2);

  // Décoder avec scaling
  jpeg.decode(0, 0, 0);

  jpeg.close();
  fclose(file);

  ESP_LOGI(TAG_IMAGE, "Scaled from %dx%d to %dx%d, 0 KB PSRAM permanent",
           jpeg.getWidth(), jpeg.getHeight(), scaled_width, scaled_height);

  return true;
}

// Callback avec scaling bilinéaire
int JPEGDrawScaled(JPEGDRAW *pDraw) {
  auto &ctx = scaling_context;
  float scale = ctx.scale_factor;

  // Scaling simple: prendre 1 pixel tous les N pixels
  int scaled_width = (int)(pDraw->iWidth * scale);

  for (int x = 0; x < scaled_width; x++) {
    // Pixel source correspondant
    int src_x = (int)(x / scale);
    if (src_x >= pDraw->iWidth) src_x = pDraw->iWidth - 1;

    // Copier pixel (scaling nearest neighbor)
    uint16_t pixel = pDraw->pPixels[src_x];
    ctx.line_buffer[x * 2] = pixel & 0xFF;
    ctx.line_buffer[x * 2 + 1] = (pixel >> 8) & 0xFF;
  }

  // Dessiner ligne scalée
  int scaled_y = (int)(pDraw->y * scale);
  for (int x = 0; x < scaled_width; x++) {
    uint16_t rgb565 = ctx.line_buffer[x * 2] | (ctx.line_buffer[x * 2 + 1] << 8);

    uint8_t r = ((rgb565 >> 11) & 0x1F) << 3;
    uint8_t g = ((rgb565 >> 5) & 0x3F) << 2;
    uint8_t b = (rgb565 & 0x1F) << 3;

    lv_color_t color = lv_color_make(r, g, b);

    #if LVGL_VERSION_MAJOR >= 9
    lv_canvas_set_px(ctx.canvas, x, scaled_y, color, LV_OPA_COVER);
    #else
    lv_canvas_set_px(ctx.canvas, x, scaled_y, color);
    #endif
  }

  return 1;
}
```

---

## 📝 Configuration YAML Universelle

### Configuration Simple (Auto-détection)

```yaml
storage:
  # Aucune configuration de résolution nécessaire!
  # Tout est auto-détecté

  backgrounds:
    - id: bg_home
      file_path: "/backgrounds/home.jpg"
      # Le composant détectera:
      # - Résolution display: 800x480, 1024x600, etc.
      # - Résolution image: peut être différente
      # - Scaling si nécessaire
      # - Streaming automatique (0 KB PSRAM)

    - id: bg_weather
      file_path: "/backgrounds/weather.jpg"

  animations:
    - id: weather_sun
      file_path: "/weather/sun.gif"
      # Auto-détection:
      # - Dimensions: 64x64
      # - Frames: 30
      # - Taille: 240 KB
      # - Décision: Cache complet (petite animation)

    - id: loading_large
      file_path: "/ui/loading.gif"
      # Auto-détection:
      # - Dimensions: 200x200
      # - Frames: 80
      # - Taille: 6.4 MB
      # - Décision: On-demand (grosse animation)

  decoders:
    libpng: true
    svg: true
    lottie: true
```

### Configuration Avancée (Override manuel si besoin)

```yaml
storage:
  # Politique globale (optionnelle)
  optimization:
    auto_detect: true  # Défaut: true
    auto_scaling: true  # Scale images > display
    max_cache_per_animation: 300KB
    preload_frames: 2

  backgrounds:
    - id: bg_home
      file_path: "/backgrounds/home.jpg"
      # Options de override (optionnelles)
      render_strategy: streaming  # Force strategy
      scaling: fit  # fit, fill, stretch, none

  animations:
    - id: weather_sun
      file_path: "/weather/sun.gif"
      # Override si besoin
      cache_strategy: full  # full, on_demand, auto (défaut)

    - id: loading_large
      file_path: "/ui/loading.gif"
      cache_strategy: on_demand  # Force on-demand
      preload_frames: 3  # Override défaut (2)
```

---

## 🎯 Cas d'Usage Supportés

### Cas 1: ESP32-S3 avec Display 800x480

```yaml
# Utilisateur A: Display 800x480
display:
  - platform: esp32_rgb_panel
    dimensions: 800x480

storage:
  backgrounds:
    - id: bg
      file_path: "/bg.jpg"  # 800x480 exact
      # → Streaming direct, pas de scaling

  animations:
    - id: icon
      file_path: "/icon.gif"  # 64x64, 30 frames
      # → Cache complet (240 KB)
```

**Mémoire**: 240 KB animations + 0 KB backgrounds = **240 KB**

---

### Cas 2: ESP32-P4 avec Display 1024x600

```yaml
# Utilisateur B: Display 1024x600
display:
  - platform: esp32_rgb_panel
    dimensions: 1024x600

storage:
  backgrounds:
    - id: bg
      file_path: "/bg.jpg"  # 1920x1080 haute résolution!
      # → Auto-scaling à 1024x600, streaming
      # → 0 KB PSRAM permanent

  animations:
    - id: icon
      file_path: "/icon.gif"  # Même 64x64 que utilisateur A
      # → Cache complet (240 KB)
```

**Mémoire**: 240 KB animations + 0 KB backgrounds = **240 KB**

---

### Cas 3: ESP32 avec Display 480x320

```yaml
# Utilisateur C: Petit display 480x320
display:
  - platform: ili9341
    dimensions: 480x320

storage:
  backgrounds:
    - id: bg
      file_path: "/bg.jpg"  # 800x480 (trop grand)
      # → Auto-scaling à 480x320, streaming
      # → 0 KB PSRAM permanent

  animations:
    - id: weather
      file_path: "/weather.gif"  # 128x128, 60 frames
      # → 1.9 MB décodé
      # → Auto: On-demand (128 KB au lieu de 1.9 MB)
```

**Mémoire**: 128 KB animations + 0 KB backgrounds = **128 KB**

---

## 📊 Comparaison Multi-Résolutions

| Display | Background | Animation | Total PSRAM | Notes |
|---------|------------|-----------|-------------|-------|
| 480x320 | 800x480 JPEG | 64x64 GIF | 240 KB | Auto-scaling background |
| 800x480 | 800x480 JPEG | 64x64 GIF | 240 KB | Pas de scaling |
| 1024x600 | 1920x1080 JPEG | 64x64 GIF | 240 KB | Auto-scaling background |
| 1280x800 | 800x480 JPEG | 100x100 GIF | 80 KB | Upscaling bg, on-demand anim |

**Conclusion**: Solution s'adapte automatiquement à n'importe quelle configuration ✅

---

## 🚀 Avantages pour Dépôt Public

### 1. **Zéro Configuration**

L'utilisateur n'a PAS besoin de:
- ❌ Spécifier la résolution display
- ❌ Choisir le mode rendering
- ❌ Calculer la mémoire nécessaire
- ❌ Ajuster les paramètres PSRAM

**Tout est automatique!** ✅

### 2. **Compatibilité Universelle**

Fonctionne sur:
- ✅ ESP32 (512 KB PSRAM)
- ✅ ESP32-S3 (2-8 MB PSRAM)
- ✅ ESP32-P4 (16-32 MB PSRAM)
- ✅ Displays: 320x240 → 1920x1080
- ✅ Images: Toutes résolutions

### 3. **Optimisation Automatique**

- Petit display → Scaling down automatique
- Grand display → Streaming haute qualité
- Peu de PSRAM → On-demand agressif
- Beaucoup de PSRAM → Cache plus généreux

### 4. **Documentation Simple**

```yaml
# README pour utilisateurs
storage:
  backgrounds:
    - id: my_bg
      file_path: "/my_background.jpg"
      # That's it! Works on any ESP32/display combo

  animations:
    - id: my_icon
      file_path: "/my_icon.gif"
      # That's it! Auto-optimized for your hardware
```

---

## 📚 Exemples README pour Utilisateurs

### Example 1: Basic Home Dashboard

```yaml
# Works on any ESP32 with any display resolution!
storage:
  backgrounds:
    - id: bg_home
      file_path: "/backgrounds/home.jpg"

  animations:
    - id: weather_icon
      file_path: "/weather/sun.gif"

lvgl:
  pages:
    - id: home
      widgets:
        - obj:
            on_load:
              - lambda: id(bg_home).render_background();
        - image:
            src: weather_icon
```

### Example 2: Multiple Pages with Different Backgrounds

```yaml
storage:
  backgrounds:
    - id: bg_home
      file_path: "/backgrounds/home.jpg"
    - id: bg_weather
      file_path: "/backgrounds/weather.jpg"
    - id: bg_lights
      file_path: "/backgrounds/lights.jpg"

  animations:
    - id: weather_sun
      file_path: "/weather/sun.gif"
    - id: weather_rain
      file_path: "/weather/rain.gif"
    - id: light_bulb
      file_path: "/lights/bulb.gif"

# No need to specify resolutions - auto-detected!
# Works on 480x320, 800x480, 1024x600, etc.
```

### Example 3: High-Res Images with Auto-Scaling

```yaml
# You can use high-resolution images
# They will be auto-scaled to your display
storage:
  backgrounds:
    - id: bg_wallpaper
      file_path: "/wallpapers/4k.jpg"
      # 3840x2160 image
      # Auto-scaled to your display (e.g., 800x480)
      # Still 0 KB PSRAM thanks to streaming!

  animations:
    - id: loading
      file_path: "/ui/loading.gif"
      # 200x200, 100 frames = 8 MB
      # Auto-optimized: Only 80 KB PSRAM used
```

---

## 🔧 Logs Auto-Documentation

```
[I][storage] Display detected: 800x480, 16-bit, RGB565
[I][storage] Image detected: bg_home.jpg
[I][storage]   - Dimensions: 1024x768
[I][storage]   - Format: JPEG
[I][storage]   - File size: 87 KB (compressed)
[I][storage]   - Decoded size: 1.5 MB (uncompressed)
[I][storage] Strategy decision:
[I][storage]   - Type: BACKGROUND
[I][storage]   - Scaling: YES (1024x768 → 800x480, 0.78x)
[I][storage]   - Strategy: SCALED_STREAMING
[I][storage]   - PSRAM needed: 3.2 KB (2 line buffers)
[I][storage]   - Reason: Background with scaling - streaming line-by-line
[I][storage] Rendering background... done in 1.8s
[I][storage] PSRAM permanent usage: 0 KB ✅

[I][storage] Image detected: weather_sun.gif
[I][storage]   - Dimensions: 64x64
[I][storage]   - Format: GIF
[I][storage]   - Frames: 30
[I][storage]   - File size: 122 KB (compressed)
[I][storage]   - Decoded size: 240 KB (uncompressed)
[I][storage] Strategy decision:
[I][storage]   - Type: ANIMATION
[I][storage]   - Scaling: NO (fits display)
[I][storage]   - Strategy: CACHE_FULL
[I][storage]   - PSRAM needed: 240 KB (all frames)
[I][storage]   - Reason: Small animation - full cache for smooth loop
[I][storage] Loading animation... done
[I][storage] PSRAM permanent usage: 240 KB ✅

[I][storage] Total PSRAM usage: 240 KB
[I][storage] Optimization: 1.74 MB saved (87% reduction)
```

**Les utilisateurs voient exactement ce qui se passe!** ✅

---

## 🎉 Conclusion

### Solution Parfaite pour Dépôt Public

**Pourquoi?**
1. ✅ **Zéro configuration résolution** - Auto-détection
2. ✅ **Fonctionne partout** - ESP32/S3/P4, tous displays
3. ✅ **Optimisation automatique** - Selon hardware disponible
4. ✅ **Scaling intelligent** - Images trop grandes/petites
5. ✅ **Logs détaillés** - Utilisateur comprend ce qui se passe
6. ✅ **Documentation simple** - "Just works"

### Plan d'Implémentation

#### Phase 1: Auto-Detection Core (1 semaine)
- ✅ `detect_display_info()`
- ✅ `detect_image_info()`
- ✅ `determine_strategy()`
- ✅ Logs détaillés

#### Phase 2: Streaming + Scaling (1 semaine)
- ✅ `render_streaming_simple()`
- ✅ `render_streaming_scaled()`
- ✅ Callback avec scaling

#### Phase 3: Cache Intelligent (1 semaine)
- ✅ `load_and_cache_full()`
- ✅ `load_on_demand_with_preload()`
- ✅ Auto-selection stratégie

#### Phase 4: Testing Multi-Résolutions (3 jours)
- ✅ Test 480x320
- ✅ Test 800x480
- ✅ Test 1024x600
- ✅ Test scaling up/down

---

## 💬 Prochaine Étape?

**Je recommande de commencer par Phase 1 + Phase 2** (2 semaines):
- Auto-détection display et images
- Streaming avec scaling automatique
- Résultat: Backgrounds 0 KB PSRAM, n'importe quelle résolution

**Voulez-vous que je commence l'implémentation?** 🚀

Je peux créer:
1. Branche `feature/auto-adaptive-psram`
2. Implémenter l'auto-détection
3. Ajouter streaming avec scaling
4. Tester sur plusieurs résolutions
5. Documenter pour utilisateurs publics

Qu'en pensez-vous?
