# Solutions Alternatives à l'Utilisation PSRAM pour Images SD

## 🔴 Problème Actuel

### Situation

Le composant `storage` charge les images depuis SD (✅) mais les **décode entièrement en PSRAM** (❌):

```cpp
// storage.cpp - ligne 516
std::vector<uint8_t> file_data = read_file_direct(path);  // Charge fichier compressé
decode_image(file_data);  // Décode TOUT en PSRAM

// Pour JPEG 800x600 RGB565:
// - Fichier JPEG: ~50 KB (compressé)
// - Décodé PSRAM: 960 KB (800 * 600 * 2 bytes)
// Ratio: 19x plus grand!

// Pour GIF animé 320x240 @ 60 frames:
// - Fichier GIF: ~500 KB (compressé)
// - Décodé PSRAM: ~9 MB (320 * 240 * 2 * 60 frames)
// Ratio: 18x plus grand!
```

### Impact Mémoire

| Format | Résolution | Frames | Fichier SD | PSRAM Décodé | Ratio |
|--------|------------|--------|------------|--------------|-------|
| JPEG | 800x600 | 1 | ~50 KB | 960 KB | **19x** |
| JPEG | 1024x768 | 1 | ~80 KB | 1.5 MB | **19x** |
| GIF | 320x240 | 30 | ~300 KB | 4.6 MB | **15x** |
| GIF | 320x240 | 60 | ~500 KB | **9.2 MB** | **18x** |
| GIF | 480x320 | 30 | ~600 KB | 9.2 MB | **15x** |

**Conclusion**: Le décodage en PSRAM annule complètement l'économie de la SD! 🔴

---

## ✅ Solutions Possibles

### Solution 1: 🏆 **Streaming Line-by-Line** (RECOMMANDÉE)

#### Concept

Décoder l'image **ligne par ligne** directement vers le display, sans buffer complet en PSRAM.

```
┌─────────────────────────────────────────────┐
│  Image JPEG sur SD (compressée 50 KB)       │
└─────────────────┬───────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────┐
│  Decoder JPEGDEC - Décode 1 ligne (1.6 KB)  │
└─────────────────┬───────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────┐
│  Display Framebuffer (géré par LVGL)        │
└─────────────────────────────────────────────┘

PSRAM utilisé: ~2 KB (buffer 1 ligne) au lieu de 960 KB!
```

#### Avantages

- ✅ **PSRAM minimal**: Seulement 1-2 lignes en mémoire (~2-4 KB)
- ✅ **Fonctionne avec JPEGDEC**: Supporte déjà le callback ligne par ligne
- ✅ **Rapide**: Pas de copie de gros buffers
- ✅ **Scalable**: Fonctionne pour n'importe quelle résolution

#### Implémentation

```cpp
// storage.cpp - Nouvelle méthode
bool SdImageComponent::decode_jpeg_streaming(const std::string &path) {
  // 1. Ouvrir le fichier sans le charger entièrement
  FILE *file = fopen(path.c_str(), "rb");
  if (!file) return false;

  // 2. Initialiser JPEGDEC avec callback streaming
  JPEGDEC jpeg;
  if (jpeg.open(file, JPEGDraw_callback_streaming) != 1) {
    fclose(file);
    return false;
  }

  int width = jpeg.getWidth();
  int height = jpeg.getHeight();

  // 3. Allouer buffer pour UNE SEULE ligne (1.6 KB pour 800px)
  this->line_buffer_.resize(width * 2);  // RGB565 = 2 bytes/pixel

  // 4. Décoder ligne par ligne
  jpeg.decode(0, 0, 0);  // Décode tout, mais par lignes via callback

  jpeg.close();
  fclose(file);
  return true;
}

// Callback appelé pour chaque ligne décodée
int JPEGDraw_callback_streaming(JPEGDRAW *pDraw) {
  SdImageComponent *img = current_image_component;

  // Copier la ligne dans line_buffer_
  memcpy(img->line_buffer_.data(), pDraw->pPixels, pDraw->iWidth * 2);

  // Envoyer DIRECTEMENT au display (via LVGL ou driver)
  // Option A: LVGL canvas
  img->draw_line_to_canvas(pDraw->y, img->line_buffer_.data());

  // Option B: Direct display driver
  // img->display_->draw_pixels_at(0, pDraw->y, pDraw->iWidth, 1,
  //                               img->line_buffer_.data());

  return 1;  // Continue
}
```

#### Configuration YAML

```yaml
storage:
  streaming_mode: true  # Active le mode streaming (pas de buffer complet)

  sd_images:
    - id: large_photo
      file_path: "/photos/large.jpg"  # 1024x768 JPEG
      streaming: true  # Decode ligne par ligne
```

#### Limitations

- ⚠️ **Pas de random access**: Ne peut pas lire un pixel au milieu sans redécoder
- ⚠️ **Pas de cache**: Chaque affichage = nouveau décodage complet
- ⚠️ **GIF streaming complexe**: LZW décompression nécessite contexte

---

### Solution 2: 🎯 **On-Demand GIF Frames**

#### Concept

Pour GIF animés, ne garder en PSRAM que **la frame courante**, pas toutes les frames.

```
┌──────────────────────────────────────────────┐
│  GIF animé sur SD (60 frames, 500 KB)        │
└───────────────────┬──────────────────────────┘
                    │
                    ▼
┌──────────────────────────────────────────────┐
│  Decoder GIF - Extrait metadata seulement     │
│  - Frame offsets: [0x120, 0x2F4, 0x4A8...]   │
│  - Frame delays: [100ms, 100ms, 50ms...]     │
└───────────────────┬──────────────────────────┘
                    │
                    ▼
┌──────────────────────────────────────────────┐
│  PSRAM: 1 frame seulement (153 KB)           │
│  - Frame 0: Décodée et affichée              │
│  - Frame 1-59: Sur SD, non décodées          │
└──────────────────────────────────────────────┘

Quand next_frame():
1. Chercher offset frame suivante sur SD
2. Décoder seulement cette frame
3. Remplacer le buffer PSRAM

PSRAM: 153 KB au lieu de 9.2 MB (60x économie!)
```

#### Avantages

- ✅ **60x moins de PSRAM**: 1 frame au lieu de 60
- ✅ **Animations fluides**: Cache la frame courante pour affichage rapide
- ✅ **Fonctionne pour tous GIF**: Pas de limite de frames

#### Implémentation

```cpp
// storage.h - Nouvelle structure
struct GifFrameMetadata {
  size_t file_offset;    // Position dans le fichier SD
  size_t compressed_size; // Taille compressée
  uint16_t delay_ms;     // Délai d'affichage
  bool has_transparency;
  uint8_t disposal_method;
};

class SdImageComponent : public Component, public image::Image {
private:
  // Au lieu de stocker toutes les frames décodées:
  // std::vector<GifFrame> gif_frames_;  // SUPPRIMÉ (9 MB)

  // Stocker seulement les métadonnées (60 frames * 20 bytes = 1.2 KB)
  std::vector<GifFrameMetadata> gif_frame_metadata_;

  // Buffer pour LA frame courante seulement (153 KB)
  std::vector<uint8_t> current_frame_buffer_;

  // Fichier SD ouvert pour accès rapide
  FILE *gif_file_handle_;
};

// storage.cpp
bool SdImageComponent::decode_gif_metadata(const std::string &path) {
  // 1. Ouvrir le fichier GIF
  gif_file_handle_ = fopen(path.c_str(), "rb");

  // 2. Parser header GIF
  GifDecoder decoder;
  decoder.open(gif_file_handle_);

  // 3. Extraire metadata SEULEMENT (pas de décodage)
  while (decoder.has_next_frame()) {
    GifFrameMetadata meta;
    meta.file_offset = decoder.get_current_position();
    meta.compressed_size = decoder.get_frame_size();
    meta.delay_ms = decoder.get_delay();
    meta.has_transparency = decoder.has_transparency();

    gif_frame_metadata_.push_back(meta);

    decoder.skip_to_next_frame();  // Ne décode PAS
  }

  ESP_LOGI(TAG_IMAGE, "GIF metadata: %zu frames, file stays on SD",
           gif_frame_metadata_.size());

  // 4. Décoder seulement la première frame
  decode_gif_frame(0);

  return true;
}

void SdImageComponent::decode_gif_frame(size_t frame_index) {
  if (frame_index >= gif_frame_metadata_.size()) return;

  const GifFrameMetadata &meta = gif_frame_metadata_[frame_index];

  // 1. Chercher la position dans le fichier SD
  fseek(gif_file_handle_, meta.file_offset, SEEK_SET);

  // 2. Lire seulement cette frame compressée (~8 KB)
  std::vector<uint8_t> compressed_frame(meta.compressed_size);
  fread(compressed_frame.data(), 1, meta.compressed_size, gif_file_handle_);

  // 3. Décoder dans current_frame_buffer_ (153 KB)
  GifDecoder decoder;
  decoder.decode_frame(compressed_frame, current_frame_buffer_);

  // 4. Mettre à jour pointeur pour affichage
  this->data_start_ = current_frame_buffer_.data();
  this->current_gif_frame_ = frame_index;
}

void SdImageComponent::next_frame() {
  size_t next = (current_gif_frame_ + 1) % gif_frame_metadata_.size();
  decode_gif_frame(next);  // Décode à la volée depuis SD
}
```

#### Configuration YAML

```yaml
storage:
  sd_images:
    - id: animated_loader
      file_path: "/animations/loader.gif"
      gif_mode: on_demand  # Ne charge pas toutes les frames
      gif_preload_frames: 3  # Précharge 3 frames pour fluidité
```

#### Limitations

- ⚠️ **Latence frame switching**: ~20-50ms pour décoder nouvelle frame depuis SD
- ⚠️ **Saccades possibles**: Si SD lente ou frame complexe
- ✅ **Solution**: Précharger 2-3 frames suivantes en arrière-plan

---

### Solution 3: 🎨 **Tiled Images** (Pour Grandes Images)

#### Concept

Découper les grandes images en **tiles** (tuiles) et ne charger que les tiles visibles à l'écran.

```
Grande image 1920x1080 = 4 MB PSRAM
Découpée en tiles 240x240 = 32 tiles

┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
│ T0  │ T1  │ T2  │ T3  │ T4  │ T5  │ T6  │ T7  │ (240x240 chaque)
├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤
│ T8  │ T9  │ T10 │ T11 │ T12 │ T13 │ T14 │ T15 │
├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤
│ T16 │ T17 │ T18 │ T19 │ T20 │ T21 │ T22 │ T23 │
├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤
│ T24 │ T25 │ T26 │ T27 │ T28 │ T29 │ T30 │ T31 │
└─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘

Écran 800x480 affiche seulement 4-6 tiles à la fois
PSRAM: 115 KB (1 tile) * 6 = 690 KB au lieu de 4 MB
```

#### Avantages

- ✅ **6x moins de PSRAM** pour grandes images
- ✅ **Zoom/Pan efficace**: Charge seulement ce qui est visible
- ✅ **Scrolling fluide**: Précharge tiles adjacentes

#### Implémentation

Créer un outil pour découper les images:

```bash
# Préparer les tiles (offline, sur PC)
python tools/tile_image.py photos/large.jpg --tile-size 240 --output tiles/

# Génère:
# tiles/large_0_0.jpg  (coin haut-gauche)
# tiles/large_0_1.jpg
# tiles/large_1_0.jpg
# ...
# tiles/large_metadata.json  (positions, dimensions)
```

Configuration:

```yaml
storage:
  tiled_images:
    - id: large_map
      base_path: "/tiles/map"
      tile_size: 240
      dimensions: "1920x1080"
      cache_tiles: 6  # Garde 6 tiles en PSRAM
```

#### Limitations

- ⚠️ **Préparation requise**: Images doivent être découpées avant
- ⚠️ **Complexité**: Gestion cache de tiles
- ⚠️ **Espace SD**: 32 fichiers au lieu de 1

---

### Solution 4: 💾 **Compressed Buffer + Decode on Draw**

#### Concept

Garder l'image **compressée** (JPEG/GIF) en PSRAM et décoder seulement pendant l'affichage.

```
┌────────────────────────────────────────────┐
│  PSRAM: Image JPEG compressée (50 KB)      │
└─────────────────┬──────────────────────────┘
                  │
                  ▼ (chaque frame display)
┌────────────────────────────────────────────┐
│  Decode temporary: 1-2 lignes (2 KB)       │
└─────────────────┬──────────────────────────┘
                  │
                  ▼
┌────────────────────────────────────────────┐
│  Display Framebuffer                        │
└────────────────────────────────────────────┘

PSRAM: 50 KB (compressé) + 2 KB (buffer ligne)
Au lieu de: 960 KB (décodé complet)
Économie: 18x
```

#### Avantages

- ✅ **18x moins de PSRAM**
- ✅ **Accès depuis PSRAM**: Plus rapide que SD
- ✅ **Pas de préparation**: Images JPEG standard

#### Implémentation

```cpp
class SdImageComponent : public Component, public image::Image {
private:
  // Au lieu de stocker l'image décodée:
  // std::vector<uint8_t> image_buffer_;  // 960 KB

  // Stocker l'image compressée
  std::vector<uint8_t> compressed_buffer_;  // 50 KB
  ImageFormat compression_format_;  // JPEG, GIF, etc.

  // Buffer temporaire pour décodage ligne par ligne
  std::vector<uint8_t> decode_line_buffer_;  // 2 KB
};

void SdImageComponent::draw(int x, int y, display::Display *display) override {
  // Décoder à la volée pendant le draw
  if (compression_format_ == ImageFormat::JPEG) {
    decode_and_draw_jpeg_streaming(x, y, display);
  }
}

void SdImageComponent::decode_and_draw_jpeg_streaming(int x, int y, display::Display *display) {
  // 1. Initialiser decoder avec buffer compressé
  JPEGDEC jpeg;
  jpeg.openRAM(compressed_buffer_.data(), compressed_buffer_.size(), JPEGDraw_callback);

  // 2. Décoder ligne par ligne directement vers display
  jpeg.decode(x, y, 0);  // Callback draw chaque ligne

  jpeg.close();
}
```

#### Configuration

```yaml
storage:
  sd_images:
    - id: photo
      file_path: "/photo.jpg"
      cache_mode: compressed  # Garde en JPEG compressé dans PSRAM
      cache_size: 200KB  # Limite cache
```

#### Limitations

- ⚠️ **CPU**: Redécode à chaque affichage (30-50ms par frame)
- ⚠️ **FPS**: Limite à ~20 FPS pour images complexes
- ✅ **OK pour**: Images statiques ou animations lentes (1-5 FPS)

---

### Solution 5: 🖥️ **Direct-to-Framebuffer Rendering**

#### Concept

Décoder **directement dans le framebuffer du display**, bypasser PSRAM complètement.

```
┌────────────────────────────────────────────┐
│  Image JPEG sur SD (50 KB)                  │
└─────────────────┬──────────────────────────┘
                  │
                  ▼
┌────────────────────────────────────────────┐
│  Decoder JPEGDEC (ligne par ligne)          │
└─────────────────┬──────────────────────────┘
                  │
                  ▼
┌────────────────────────────────────────────┐
│  Display Framebuffer (DMA direct)           │
│  LVGL draw_buffer ou SPI DMA                │
└────────────────────────────────────────────┘

PSRAM utilisé: 0 KB! ✅
```

#### Avantages

- ✅ **Zéro PSRAM**: Pas de buffer intermédiaire
- ✅ **Plus rapide**: DMA direct
- ✅ **Hardware accelerated**: Si ESP32-P4 2D-PPA disponible

#### Implémentation

```cpp
// Nécessite accès direct au display driver
void SdImageComponent::draw_direct_to_display(display::Display *display) {
  // 1. Obtenir le framebuffer du display
  void *framebuffer = display->get_buffer();
  int fb_width = display->get_width();
  int fb_height = display->get_height();

  // 2. Décoder ligne par ligne dans le framebuffer
  JPEGDEC jpeg;
  jpeg.openFile(file_path_.c_str());

  for (int y = 0; y < jpeg.getHeight(); y++) {
    // Calculer position dans le framebuffer
    uint16_t *fb_line = (uint16_t *)framebuffer + y * fb_width;

    // Décoder ligne directement dans le framebuffer
    jpeg.decodeLine(fb_line);
  }

  jpeg.close();

  // 3. Flush DMA si nécessaire
  display->flush_buffer();
}
```

#### Configuration

```yaml
display:
  - platform: esp32_rgb_panel
    id: main_display
    dimensions: 800x480
    direct_rendering: true  # Active rendu direct

storage:
  sd_images:
    - id: background
      file_path: "/bg.jpg"
      render_mode: direct_fb  # Rendu direct framebuffer
```

#### Limitations

- ⚠️ **Dépend du display**: Nécessite accès direct au framebuffer
- ⚠️ **Pas de compositing**: Impossible de combiner plusieurs images
- ⚠️ **LVGL complexe**: Difficile à intégrer avec LVGL layers
- ✅ **Parfait pour**: Background fullscreen, splash screens

---

## 📊 Comparaison des Solutions

| Solution | PSRAM Économisé | Complexité | Performance | Cas d'Usage |
|----------|----------------|------------|-------------|-------------|
| **1. Streaming Line-by-Line** | **99%** (2 KB vs 960 KB) | 🟢 Faible | ✅ Rapide | Images statiques moyennes/grandes |
| **2. On-Demand GIF Frames** | **98%** (153 KB vs 9 MB) | 🟡 Moyenne | ⚠️ Latence 20-50ms | GIF animés |
| **3. Tiled Images** | **85%** (690 KB vs 4 MB) | 🔴 Élevée | ✅ Rapide avec cache | Très grandes images, zoom/pan |
| **4. Compressed Buffer** | **95%** (52 KB vs 960 KB) | 🟢 Faible | ⚠️ Redecode chaque draw | Images statiques, updates rares |
| **5. Direct Framebuffer** | **100%** (0 KB) | 🔴 Élevée | ✅ Très rapide | Fullscreen, backgrounds |

---

## 🎯 Recommandations par Cas d'Usage

### Cas 1: Photos Statiques (800x600 JPEG)

**Solution recommandée**: **#1 Streaming Line-by-Line**

```yaml
storage:
  sd_images:
    - id: photo
      file_path: "/photo.jpg"
      render_mode: streaming  # Active streaming
```

**Résultat**: 2 KB PSRAM au lieu de 960 KB (480x économie)

---

### Cas 2: GIF Animés (320x240 @ 30 FPS)

**Solution recommandée**: **#2 On-Demand Frames + Preload**

```yaml
storage:
  sd_images:
    - id: animated_icon
      file_path: "/icons/loading.gif"
      gif_mode: on_demand
      preload_frames: 2  # Précharge 2 frames pour fluidité
```

**Résultat**: 460 KB PSRAM (3 frames) au lieu de 4.6 MB (30 frames) - 10x économie

---

### Cas 3: Background Fullscreen (1024x600)

**Solution recommandée**: **#5 Direct Framebuffer** ou **#1 Streaming**

```yaml
storage:
  sd_images:
    - id: background
      file_path: "/backgrounds/home.jpg"
      render_mode: direct_fb  # Si display supporte
```

**Résultat**: 0 KB PSRAM

---

### Cas 4: Grande Image avec Zoom/Pan (1920x1080)

**Solution recommandée**: **#3 Tiled Images**

```bash
# Préparation (offline)
python tools/tile_image.py map.jpg --tile-size 240

# Configuration
storage:
  tiled_images:
    - id: world_map
      base_path: "/tiles/map"
      tile_size: 240
      cache_tiles: 6
```

**Résultat**: 690 KB PSRAM au lieu de 4 MB (6x économie)

---

## 🚀 Plan d'Implémentation

### Phase 1: Streaming JPEG (Court Terme)

**Objectif**: Réduire PSRAM de 99% pour images statiques

**Tâches**:
1. ✅ Modifier `decode_jpeg_image()` pour mode streaming
2. ✅ Implémenter `JPEGDraw_callback_streaming()`
3. ✅ Ajouter buffer ligne unique `line_buffer_`
4. ✅ Tester avec image 800x600
5. ✅ Documenter différence mémoire

**Estimation**: 2-3 jours

---

### Phase 2: On-Demand GIF (Moyen Terme)

**Objectif**: Réduire PSRAM de 98% pour GIF animés

**Tâches**:
1. ✅ Créer structure `GifFrameMetadata`
2. ✅ Parser GIF pour extraire metadata seulement
3. ✅ Implémenter `decode_gif_frame(index)`
4. ✅ Modifier `next_frame()` pour décodage à la volée
5. ✅ Ajouter préchargement optionnel (2-3 frames)
6. ✅ Benchmarker latence frame switching

**Estimation**: 1 semaine

---

### Phase 3: Tiled Images (Long Terme - Optionnel)

**Objectif**: Support grandes images avec zoom/pan

**Tâches**:
1. ✅ Créer outil Python `tile_image.py`
2. ✅ Implémenter `TiledImageComponent`
3. ✅ Cache LRU pour tiles
4. ✅ Préchargement tiles adjacentes
5. ✅ API zoom/pan

**Estimation**: 2 semaines

---

## 📝 Configuration Finale Recommandée

```yaml
storage:
  # Configuration globale
  optimization_mode: low_psram  # Active toutes les optimisations

  # Images JPEG statiques - Streaming
  sd_images:
    - id: photo1
      file_path: "/photos/photo1.jpg"
      render_mode: streaming  # 2 KB PSRAM

    - id: photo2
      file_path: "/photos/photo2.jpg"
      render_mode: streaming

  # GIF animés - On-demand frames
  sd_animations:
    - id: loader
      file_path: "/animations/loader.gif"
      gif_mode: on_demand  # 1 frame en PSRAM
      preload_frames: 2    # + 2 frames préchargées = 460 KB

    - id: icon_animated
      file_path: "/icons/wifi.gif"
      gif_mode: on_demand
      preload_frames: 1

  # Grande image - Tiled (optionnel)
  tiled_images:
    - id: world_map
      base_path: "/tiles/map"
      tile_size: 240
      cache_tiles: 6  # 690 KB PSRAM

  # Décodeurs LVGL (pour PNG/SVG/etc via LVGL)
  decoders:
    libpng: true   # PNG géré par LVGL directement
    svg: true
    lottie: true
```

---

## 🎉 Résultat Final

### Avant Optimisation

| Type | Résolution | Fichiers | PSRAM Total |
|------|------------|----------|-------------|
| 3 JPEG | 800x600 | 3 | **2.8 MB** |
| 2 GIF | 320x240 @ 30fps | 2 | **9.2 MB** |
| **TOTAL** | | 5 fichiers | **12 MB** ❌ |

**Problème**: ESP32-S3 a seulement 8 MB PSRAM → **Impossible**

### Après Optimisation

| Type | Mode | PSRAM par Fichier | PSRAM Total |
|------|------|-------------------|-------------|
| 3 JPEG | Streaming | 2 KB × 3 | **6 KB** |
| 2 GIF | On-Demand + Preload | 460 KB × 2 | **920 KB** |
| **TOTAL** | | 5 fichiers | **926 KB** ✅ |

**Économie**: 12 MB → 926 KB = **92% de PSRAM économisé!** 🎉

---

## 🔗 Prochaines Étapes

1. **Décider quelle solution implémenter en priorité**
   - Recommandation: Commencer par #1 (Streaming JPEG)

2. **Créer une nouvelle branche**
   ```bash
   git checkout -b feature/low-psram-streaming
   ```

3. **Implémenter Phase 1** (Streaming JPEG)
   - Modifier `storage.cpp` et `storage.h`
   - Ajouter configuration YAML
   - Tester et benchmarker

4. **Documenter les résultats**
   - Avant/après mémoire
   - Performance (temps décodage)
   - FPS impact

Voulez-vous que je commence l'implémentation de la **Solution #1 (Streaming Line-by-Line)** ? 🚀
