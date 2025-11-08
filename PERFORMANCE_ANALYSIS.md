# Analyse de Performance - Streaming Vidéo MIPI-CSI

## 📊 Résultats du Profiling (720p RGB565)

### Performance Actuelle

```
📊 Profiling (avg over 100 frames):
   DQBUF: 414 us (0.4ms)
   PPA copy: 43488 us (43.5ms) ← BOTTLENECK
   QBUF: 67 us (0.07ms)
   TOTAL: 44.0 ms → ~22 FPS
```

**Bande passante PPA:** 1,843,200 bytes / 43,488 us = **42.4 MB/s**

### Performance M5Stack Démo (Référence)

D'après l'utilisateur: **>30 FPS avec 720p** (même résolution)

Cela implique que le PPA devrait copier en **<10ms** pour atteindre 30 FPS.

## 🔍 Différences à Investiguer

### 1. Configuration PPA

**Notre code actuel:**
```cpp
ppa_srm_oper_config_t srm_config = {
  .in = {
    .buffer = src,
    .pic_w = 1280,
    .pic_h = 720,
    .block_w = 1280,
    .block_h = 720,
    .block_offset_x = 0,
    .block_offset_y = 0,
    .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
  },
  .out = {
    .buffer = this->image_buffer_,
    .buffer_size = this->image_buffer_size_,
    .pic_w = 1280,
    .pic_h = 720,
    .block_offset_x = 0,
    .block_offset_y = 0,
    .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
  },
  .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
  .scale_x = 1.0f,
  .scale_y = 1.0f,
  .mirror_x = false,
  .mirror_y = false,
  .rgb_swap = false,
  .byte_swap = false,
  .mode = PPA_TRANS_MODE_BLOCKING,
};
```

**M5Stack:**
```cpp
ppa_srm_oper_config_t srm_config = {
  // ... mêmes paramètres ...
  .scale_x = 1,      // ← ENTIER au lieu de float
  .scale_y = 1,      // ← ENTIER au lieu de float
  .mirror_x = true,  // ← ILS UTILISENT mirror
  .mode = PPA_TRANS_MODE_BLOCKING,
};
```

**Différence potentielle:** `scale_x/scale_y` en **int** vs **float**?

### 2. Allocation Mémoire

**Notre code:**
```cpp
this->image_buffer_ = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
```

**M5Stack:**
```cpp
img_show_data = (uint8_t*)heap_caps_calloc(size, 1, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
```

**Différence:** `malloc` vs `calloc` (zero-init) - ne devrait pas affecter la performance.

### 3. Buffers V4L2 Source

**Question:** Les buffers V4L2 MMAP sont-ils dans une zone mémoire optimale pour le PPA?

Notre mmap:
```cpp
v4l2_buffers_[i].start = mmap(NULL, buf.length,
                              PROT_READ | PROT_WRITE,
                              MAP_SHARED, video_fd_, buf.m.offset);
```

Où sont ces buffers dans la mémoire? SRAM? PSRAM? Cache externe?

### 4. Configuration du Sensor/ISP

**Hypothèse:** Peut-être que M5Stack configure le sensor différemment pour optimiser la performance?

À vérifier:
- Framerate du sensor (30 FPS réel?)
- Configuration ISP (format de sortie optimisé?)
- Pipeline V4L2 (buffers, queue depth?)

## 🎯 Pistes de Solution

### Option A: Optimiser la Configuration PPA

**À essayer:**
1. Utiliser `scale_x = 1` et `scale_y = 1` (int) au lieu de `1.0f`
2. Tester `PPA_TRANS_MODE_NON_BLOCKING` avec callback
3. Vérifier si `mirror_x = true` change la performance

### Option B: Optimiser l'Allocation Mémoire

**À essayer:**
1. Forcer le buffer destination dans SRAM interne (`MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA`)
2. Utiliser un buffer plus petit et faire du scaling dans LVGL
3. Essayer `heap_caps_aligned_alloc()` avec alignement 64 bytes

### Option C: Zero-Copy (Risque de Tearing)

Utiliser directement les buffers V4L2 MMAP sans copie PPA.

**Performance attendue:** ~2ms au lieu de 43ms → **30 FPS**
**Inconvénient:** Risque de tearing

### Option D: Réduire la Résolution

| Résolution | Taille | Temps PPA (estimé) | FPS |
|------------|--------|-------------------|-----|
| 720P | 1.8MB | 43ms | ~22 FPS |
| 480P | 614KB | ~14ms | ~28 FPS |
| QVGA | 154KB | ~3.5ms | ~30 FPS |

## 📝 Actions Requises

1. **Comparer configuration M5Stack en détail:**
   - Vérifier tous les paramètres PPA
   - Vérifier la configuration du sensor
   - Vérifier le setup V4L2

2. **Tester les optimisations PPA:**
   - Changer `scale_x/y` en int
   - Tester différents modes de transfert
   - Tester différentes allocations mémoire

3. **Mesurer la performance réelle M5Stack:**
   - Profiler leur code avec esp_timer_get_time()
   - Comparer le temps PPA exact

## 🚀 Prochaine Étape Recommandée

**Tester l'option A.1 en premier:** Changer `scale_x/y` de float à int, car c'est une différence visible entre notre code et M5Stack.

Si ça ne marche pas, considérer **Option C (zero-copy)** pour matcher la performance M5Stack.
