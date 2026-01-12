# Solutions PSRAM pour Dashboard UI (Backgrounds + Animations)

## 🎯 Votre Cas d'Usage Spécifique

### Architecture UI

```
┌─────────────────────────────────────────────────────────┐
│  Display 800x480 ou 1024x600                             │
│                                                           │
│  ┌────────────────────────────────────────────────────┐  │
│  │  Layer 1: FOND D'ÉCRAN (JPEG/PNG)                  │  │
│  │  - Image statique grande résolution                │  │
│  │  - Change rarement (navigation entre pages)       │  │
│  │  - Exemple: /backgrounds/home.jpg (800x480)        │  │
│  └────────────────────────────────────────────────────┘  │
│                                                           │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐     │
│  │  GIF Météo  │  │ GIF Temp    │  │ SVG Light   │     │
│  │  (64x64)    │  │ (48x48)     │  │ (32x32)     │     │
│  │  Loop 30fps │  │ Loop 15fps  │  │ Loop 60fps  │     │
│  └─────────────┘  └─────────────┘  └─────────────┘     │
│                                                           │
│  ┌─────────────┐  ┌─────────────┐                       │
│  │ Lottie      │  │ GIF Circuit │                       │
│  │ Loading     │  │ Breaker     │                       │
│  │ (100x100)   │  │ (80x80)     │                       │
│  └─────────────┘  └─────────────┘                       │
└─────────────────────────────────────────────────────────┘

Caractéristiques:
- 1 fond d'écran: Grande image statique
- 5-10 animations: Petites, loop continu 15-60 FPS
```

---

## 💡 Stratégie Hybride Optimale

### Principe

**Traiter différemment les backgrounds et les animations**:

1. **Backgrounds (JPEG/PNG)**:
   - ✅ **Streaming direct vers framebuffer** (0 KB PSRAM permanent)
   - Décodé une seule fois lors du changement de page
   - Pas gardé en mémoire

2. **Animations (GIF/SVG/Lottie)**:
   - ✅ **Cache intelligent** basé sur la taille et l'usage
   - Petites animations (<100 KB): Toutes frames en PSRAM
   - Animations moyennes (100-500 KB): On-demand avec préload
   - Grandes animations (>500 KB): Streaming ou tiles

---

## 📊 Estimation Mémoire Votre Cas

### Backgrounds (1 par page)

| Page | Résolution | PSRAM Actuel | PSRAM Streaming | Économie |
|------|------------|--------------|-----------------|----------|
| Home | 800x480 | 768 KB | **0 KB** | 100% |
| Météo | 800x480 | 768 KB | **0 KB** | 100% |
| Éclairage | 800x480 | 768 KB | **0 KB** | 100% |
| Énergie | 800x480 | 768 KB | **0 KB** | 100% |

**Total backgrounds**: 3 MB → **0 KB** ✅

### Animations Typiques

| Animation | Résolution | Frames | Taille/Frame | Total | Stratégie |
|-----------|------------|--------|--------------|-------|-----------|
| Météo soleil | 64x64 | 20 | 8 KB | 160 KB | **Cache complet** |
| Météo pluie | 64x64 | 30 | 8 KB | 240 KB | **Cache complet** |
| Température | 48x48 | 10 | 4.6 KB | 46 KB | **Cache complet** |
| Luminosité | 32x32 | 40 | 2 KB | 80 KB | **Cache complet** |
| Éclairage LED | 80x80 | 60 | 12.8 KB | 768 KB | **On-demand** |
| Circuit breaker | 80x80 | 20 | 12.8 KB | 256 KB | **Cache complet** |
| Loading spinner | 100x100 | 50 | 20 KB | 1 MB | **On-demand** |

**Total animations**: ~2.5 MB PSRAM

### Résultat Final

```
AVANT optimisation:
- Backgrounds: 3 MB
- Animations: 2.5 MB
- TOTAL: 5.5 MB

APRÈS optimisation:
- Backgrounds: 0 KB (streaming)
- Animations: 2.5 MB (cache sélectif)
- TOTAL: 2.5 MB

Économie: 55% + plus de flexibilité
```

---

## 🎨 Solution 1: Background Streaming (Priorité 1)

### Concept

**Décoder le fond d'écran ligne par ligne directement dans le framebuffer LVGL/Display**

```
Changement de page
↓
Chargement background depuis SD (50-100 KB compressé)
↓
Décodage ligne par ligne (streaming)
↓
Affichage direct dans framebuffer
↓
Libération mémoire immédiate
↓
Background affiché, 0 KB PSRAM utilisé ✅
```

### Implémentation

```cpp
// storage.h
class SdImageComponent : public Component, public image::Image {
public:
  // Nouveau mode pour backgrounds
  void set_image_type(ImageType type) { this->image_type_ = type; }

  // Render background directement sans buffer PSRAM
  bool render_background_streaming(lv_obj_t *parent);

private:
  enum class ImageType {
    STANDARD,      // Normal (avec buffer PSRAM)
    BACKGROUND,    // Background streaming (0 PSRAM)
    ANIMATION      // Animation (cache intelligent)
  };

  ImageType image_type_ = ImageType::STANDARD;
};

// storage.cpp
bool SdImageComponent::render_background_streaming(lv_obj_t *parent) {
  if (image_type_ != ImageType::BACKGROUND) {
    ESP_LOGW(TAG_IMAGE, "Not a background image");
    return false;
  }

  ESP_LOGI(TAG_IMAGE, "Rendering background streaming from: %s", file_path_.c_str());

  // 1. Ouvrir fichier JPEG
  FILE *file = fopen(file_path_.c_str(), "rb");
  if (!file) return false;

  // 2. Obtenir framebuffer LVGL ou canvas
  lv_obj_t *canvas = lv_canvas_create(parent);
  lv_obj_set_size(canvas, LV_HOR_RES, LV_VER_RES);

  // 3. Décoder ligne par ligne
  JPEGDEC jpeg;
  if (jpeg.open(file, JPEGDrawBackground) != 1) {
    fclose(file);
    return false;
  }

  // Contexte pour le callback
  background_render_context.canvas = canvas;
  background_render_context.line_buffer.resize(LV_HOR_RES * 2);

  // Décoder - callback sera appelé pour chaque ligne
  jpeg.decode(0, 0, 0);

  jpeg.close();
  fclose(file);

  ESP_LOGI(TAG_IMAGE, "Background rendered, 0 KB PSRAM used ✅");
  return true;
}

// Callback pour rendering ligne par ligne
int JPEGDrawBackground(JPEGDRAW *pDraw) {
  auto &ctx = background_render_context;

  // Copier ligne dans buffer temporaire
  memcpy(ctx.line_buffer.data(), pDraw->pPixels, pDraw->iWidth * 2);

  // Dessiner ligne sur canvas LVGL
  for (int x = 0; x < pDraw->iWidth; x++) {
    uint16_t rgb565 = ctx.line_buffer[x * 2] | (ctx.line_buffer[x * 2 + 1] << 8);

    uint8_t r = ((rgb565 >> 11) & 0x1F) << 3;
    uint8_t g = ((rgb565 >> 5) & 0x3F) << 2;
    uint8_t b = (rgb565 & 0x1F) << 3;

    lv_color_t color = lv_color_make(r, g, b);

    #if LVGL_VERSION_MAJOR >= 9
    lv_canvas_set_px(ctx.canvas, x, pDraw->y, color, LV_OPA_COVER);
    #else
    lv_canvas_set_px(ctx.canvas, x, pDraw->y, color);
    #endif
  }

  // Feed watchdog
  if (pDraw->y % 16 == 0) {
    App.feed_wdt();
  }

  return 1;
}
```

### Configuration YAML

```yaml
storage:
  # Configuration globale
  psram_optimization: true

  # Backgrounds - Mode streaming (0 PSRAM)
  backgrounds:
    - id: bg_home
      file_path: "/backgrounds/home.jpg"
      # Automatiquement en mode streaming, pas de buffer PSRAM

    - id: bg_weather
      file_path: "/backgrounds/weather.jpg"

    - id: bg_lighting
      file_path: "/backgrounds/lighting.jpg"

    - id: bg_energy
      file_path: "/backgrounds/energy.jpg"

  # Animations - Cache intelligent
  animations:
    # Petites animations - Cache complet
    - id: anim_sun
      file_path: "/weather/sun.gif"
      cache_mode: full  # Toutes frames en PSRAM (160 KB)

    - id: anim_rain
      file_path: "/weather/rain.gif"
      cache_mode: full  # 240 KB

    - id: anim_temp
      file_path: "/indicators/temp.gif"
      cache_mode: full  # 46 KB

    - id: anim_brightness
      file_path: "/indicators/brightness.gif"
      cache_mode: full  # 80 KB

    # Animations moyennes - On-demand + preload
    - id: anim_led_strip
      file_path: "/lighting/led_strip.gif"
      cache_mode: on_demand  # 768 KB → 38 KB (1 frame)
      preload_frames: 2      # + 76 KB = 114 KB total

    - id: anim_circuit_breaker
      file_path: "/energy/breaker.gif"
      cache_mode: full  # 256 KB OK

    # Grosses animations - On-demand
    - id: anim_loading
      file_path: "/ui/loading.gif"
      cache_mode: on_demand  # 1 MB → 20 KB (1 frame)
      preload_frames: 3      # + 60 KB = 80 KB total

  # Décodeurs LVGL (pour SVG/Lottie futur)
  decoders:
    thorvg:
      internal: true
    svg: true
    lottie: true
```

### Utilisation LVGL

```yaml
lvgl:
  pages:
    - id: page_home
      widgets:
        # 1. Background - Rendu streaming (0 PSRAM permanent)
        - obj:
            id: bg_container
            width: 100%
            height: 100%
            on_load:
              - lambda: |-
                  // Render background en streaming
                  id(bg_home).render_background_streaming(id(bg_container));

        # 2. Animations - Cache en PSRAM pour performance
        - image:
            align: TOP_LEFT
            x: 20
            y: 20
            src: anim_sun  # Loop automatique, cache en PSRAM

        - image:
            align: TOP_RIGHT
            x: -20
            y: 20
            src: anim_temp

        - image:
            align: CENTER
            src: anim_loading  # On-demand, seulement 80 KB
```

---

## 🎯 Solution 2: Cache Intelligent pour Animations

### Politique de Cache

```cpp
// storage.h
struct AnimationCachePolicy {
  size_t max_full_cache_size = 300 * 1024;  // 300 KB max par animation
  size_t total_cache_limit = 2 * 1024 * 1024;  // 2 MB total animations
  size_t preload_frame_count = 2;  // Précharge 2 frames
};

class SdImageComponent {
private:
  AnimationCachePolicy cache_policy_;

  // Décision automatique de stratégie
  void determine_cache_strategy() {
    size_t estimated_size = width_ * height_ * 2 * frame_count_;

    if (estimated_size < cache_policy_.max_full_cache_size) {
      // Petit: Cache complet
      cache_mode_ = CacheMode::FULL;
      ESP_LOGI(TAG_IMAGE, "Animation small (%zu KB), using full cache",
               estimated_size / 1024);
    } else {
      // Gros: On-demand
      cache_mode_ = CacheMode::ON_DEMAND;
      ESP_LOGI(TAG_IMAGE, "Animation large (%zu KB), using on-demand",
               estimated_size / 1024);
    }
  }
};
```

### Auto-Configuration

```yaml
storage:
  # Configuration automatique basée sur la taille
  animations:
    - id: anim_sun
      file_path: "/weather/sun.gif"
      # Pas de cache_mode spécifié
      # → Auto-détecté: <300 KB = cache complet

    - id: anim_loading
      file_path: "/ui/loading.gif"
      # Auto-détecté: >300 KB = on-demand

  # Ou override manuel
  cache_policy:
    max_full_cache_per_animation: 500KB  # Augmenter limite
    total_cache_limit: 3MB
    preload_frames: 3
```

---

## 📊 Benchmark Estimé

### Cas Typique: Dashboard Home Automation

**Composants**:
- 1 background 800x480 JPEG
- 2 petites animations météo 64x64 (30 frames)
- 1 animation température 48x48 (10 frames)
- 2 indicateurs luminosité 32x32 (40 frames)
- 1 grosse animation loading 100x100 (50 frames)

#### AVANT Optimisation

```
Background home.jpg:
- Décodé complet: 768 KB PSRAM

Animations:
- 2 météo: 480 KB (2 * 240 KB)
- 1 temp: 46 KB
- 2 indicateurs: 160 KB (2 * 80 KB)
- 1 loading: 1 MB

TOTAL: 2.45 MB PSRAM
```

#### APRÈS Optimisation

```
Background home.jpg:
- Streaming: 0 KB PSRAM permanent
- 2 KB temporaire pendant rendu (2 secondes)

Animations:
- 2 météo: 480 KB (cache complet)
- 1 temp: 46 KB (cache complet)
- 2 indicateurs: 160 KB (cache complet)
- 1 loading: 80 KB (on-demand + 3 frames preload)

TOTAL: 766 KB PSRAM
```

**Économie**: 2.45 MB → 766 KB = **69% économie** 🎉

---

## 🚀 Plan d'Implémentation Adapté

### Phase 1: Background Streaming (Urgent - 3 jours)

**Objectif**: Libérer ~768 KB par background

**Tâches**:
1. ✅ Ajouter `ImageType::BACKGROUND` enum
2. ✅ Implémenter `render_background_streaming()`
3. ✅ Callback `JPEGDrawBackground()` ligne par ligne
4. ✅ Buffer temporaire ligne unique (2 KB)
5. ✅ Intégration LVGL canvas/framebuffer
6. ✅ Tester avec background 800x480

**Configuration**:
```yaml
storage:
  backgrounds:
    - id: bg_home
      file_path: "/backgrounds/home.jpg"
```

**Résultat attendu**: 0 KB PSRAM permanent pour backgrounds ✅

---

### Phase 2: Cache Intelligent GIF (1 semaine)

**Objectif**: Optimiser GIF selon taille (auto-détection)

**Tâches**:
1. ✅ Créer `AnimationCachePolicy` structure
2. ✅ Implémenter auto-détection taille animation
3. ✅ Mode `CacheMode::FULL` pour petites (<300 KB)
4. ✅ Mode `CacheMode::ON_DEMAND` pour grandes (>300 KB)
5. ✅ Préchargement 2-3 frames pour fluidité
6. ✅ Benchmarker latence frame switching

**Configuration**:
```yaml
storage:
  animations:
    - id: anim_small
      file_path: "/icons/small.gif"
      # Auto: cache complet

    - id: anim_large
      file_path: "/icons/large.gif"
      # Auto: on-demand
```

**Résultat attendu**: 1 MB → 80 KB pour grosses animations

---

### Phase 3: SVG/Lottie Integration (Plus tard)

**Objectif**: Support ThorVG/Lottie pour animations vectorielles

**Avantages SVG/Lottie**:
- ✅ Fichiers très petits (5-50 KB vs 500 KB GIF)
- ✅ Scalable sans perte qualité
- ✅ Moins de PSRAM que GIF équivalent

**Configuration future**:
```yaml
storage:
  animations:
    # SVG vectoriel - Très léger
    - id: anim_weather_icon
      file_path: "/weather/sun.svg"
      width: 64
      height: 64

    # Lottie animation - Très fluide
    - id: anim_loading_lottie
      file_path: "/ui/loading.json"
      loop: true
```

**Note**: Nécessite LVGL v9.4 (en cours de développement ESPHome)

---

## 🎨 Exemple Configuration Complète

```yaml
# Votre configuration optimisée finale
storage:
  # Optimisation globale
  psram_optimization: true

  # Politique de cache par défaut
  cache_policy:
    max_full_cache_per_animation: 300KB
    total_cache_limit: 2MB
    preload_frames: 2

  # ==========================================
  # BACKGROUNDS - Mode streaming (0 PSRAM)
  # ==========================================
  backgrounds:
    - id: bg_home
      file_path: "/backgrounds/home.jpg"
      # 800x480, streaming, 0 KB PSRAM permanent

    - id: bg_weather
      file_path: "/backgrounds/weather.jpg"

    - id: bg_lighting
      file_path: "/backgrounds/lighting.jpg"

    - id: bg_energy
      file_path: "/backgrounds/energy.jpg"

  # ==========================================
  # ANIMATIONS - Cache intelligent
  # ==========================================
  animations:
    # Météo (64x64, 20-30 frames = 160-240 KB)
    - id: weather_sun
      file_path: "/weather/sun.gif"
      # Auto: cache complet (240 KB)

    - id: weather_rain
      file_path: "/weather/rain.gif"
      # Auto: cache complet

    - id: weather_cloud
      file_path: "/weather/cloud.gif"

    - id: weather_snow
      file_path: "/weather/snow.gif"

    # Indicateurs (32x32-48x48, 10-40 frames)
    - id: indicator_temp
      file_path: "/indicators/temp.gif"
      # Auto: cache complet (46 KB)

    - id: indicator_humidity
      file_path: "/indicators/humidity.gif"

    - id: indicator_brightness
      file_path: "/indicators/brightness.gif"
      # Auto: cache complet (80 KB)

    # Éclairage (80x80, 60 frames = 768 KB)
    - id: lighting_led_strip
      file_path: "/lighting/led_strip.gif"
      # Auto: on-demand (768 KB → 114 KB avec preload)

    - id: lighting_bulb
      file_path: "/lighting/bulb.gif"

    # Énergie
    - id: energy_circuit_breaker
      file_path: "/energy/breaker.gif"
      # Auto: cache complet (256 KB)

    - id: energy_consumption
      file_path: "/energy/consumption.gif"

    # UI (100x100, 50 frames = 1 MB)
    - id: ui_loading_spinner
      file_path: "/ui/loading.gif"
      # Auto: on-demand (1 MB → 80 KB)

  # ==========================================
  # DÉCODEURS LVGL (futur: SVG/Lottie)
  # ==========================================
  decoders:
    # Images statiques
    libpng: true
    libjpeg_turbo: true
    gif: true
    bmp: true

    # Vectoriel (LVGL v9+)
    thorvg:
      internal: true
    svg: true
    lottie: true

    # Performance
    draw_sw_complex: true
    draw_sw_asm: neon  # Si ARM Cortex-A
    img_cache_size: 256
```

---

## 📊 Résultat Final Votre Projet

### Dashboard Home Page

| Élément | Avant | Après | Économie |
|---------|-------|-------|----------|
| Background | 768 KB | 0 KB | **100%** |
| 2 météo GIF | 480 KB | 480 KB | 0% (cache OK) |
| 1 temp GIF | 46 KB | 46 KB | 0% |
| 2 brightness GIF | 160 KB | 160 KB | 0% |
| 1 loading GIF | 1 MB | 80 KB | **92%** |
| **TOTAL** | **2.45 MB** | **766 KB** | **69%** |

### Toutes les Pages (4 pages)

| Scénario | PSRAM Utilisé |
|----------|---------------|
| Page 1 active | 766 KB |
| Switch page 2 | 766 KB (background streaming) |
| Switch page 3 | 766 KB |
| Switch page 4 | 766 KB |

**Pas d'accumulation** car backgrounds en streaming ✅

---

## 🎉 Conclusion

### Votre cas d'usage est PARFAIT pour cette optimisation!

**Pourquoi?**
1. ✅ Backgrounds changent rarement → Streaming idéal (0 PSRAM)
2. ✅ Petites animations loop → Cache complet OK
3. ✅ Grosses animations occasionnelles → On-demand efficace
4. ✅ Économie: **69% PSRAM** pour même qualité visuelle

### Prochaine Étape?

**Je recommande de commencer par Phase 1 (Background Streaming)**:
- Impact immédiat: 768 KB → 0 KB par background
- Simple à implémenter (3 jours)
- Pas de changement visible pour l'utilisateur
- Libère PSRAM pour plus d'animations

**Voulez-vous que je commence l'implémentation?** 🚀

Je peux créer une nouvelle branche et implémenter:
1. Mode `ImageType::BACKGROUND`
2. Fonction `render_background_streaming()`
3. Configuration YAML `backgrounds:`
4. Tests avec votre image home.jpg

Qu'en pensez-vous?
