# Investigation PPA Performance

## Comparaison Code M5Stack vs Notre Code

### Similarités ✅

| Aspect | M5Stack | Notre Code | Match? |
|--------|---------|------------|--------|
| Allocation | `MALLOC_CAP_DMA \| MALLOC_CAP_SPIRAM` | `MALLOC_CAP_DMA \| MALLOC_CAP_SPIRAM` | ✅ |
| Mode PPA | `PPA_TRANS_MODE_BLOCKING` | `PPA_TRANS_MODE_BLOCKING` | ✅ |
| Opération | `PPA_OPERATION_SRM` | `PPA_OPERATION_SRM` | ✅ |
| Format | RGB565 | RGB565 | ✅ |
| Résolution | 1280x720 | 1280x720 | ✅ |

### Différences ⚠️

| Aspect | M5Stack | Notre Code |
|--------|---------|------------|
| `mirror_x` | `true` | `false` |
| `scale_x/y` | `1` (int) | `1.0f` → `1` (corrigé) |
| Contexte | FreeRTOS task dédiée | ESPHome component loop |
| Délai | `vTaskDelay(10ms)` après chaque frame | Aucun |

## 🔍 Hypothèses sur la Lenteur du PPA

### Hypothèse 1: Zone Mémoire Non-Optimale

**Théorie:** Les buffers V4L2 MMAP sont peut-être dans une zone mémoire que le PPA accède lentement.

**Test requis:**
```cpp
// Log des adresses mémoire
ESP_LOGI(TAG, "V4L2 buffer[0]: %p (range: %p-%p)",
         v4l2_buffers_[0].start,
         v4l2_buffers_[0].start,
         (uint8_t*)v4l2_buffers_[0].start + v4l2_buffers_[0].length);
ESP_LOGI(TAG, "Image buffer: %p (range: %p-%p)",
         image_buffer_,
         image_buffer_,
         image_buffer_ + image_buffer_size_);
```

**Attendu:**
- SPIRAM: 0x48000000 - 0x4C000000
- SRAM: 0x40800000 - 0x40900000

### Hypothèse 2: Cache Non-Invalidé

**Théorie:** Le PPA DMA nécessite peut-être une invalidation de cache avant la copie.

**Test requis:**
```cpp
#include "esp_cache.h"

// Avant PPA
esp_cache_msync((void*)src, image_buffer_size_,
                ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

// PPA copy
ppa_do_scale_rotate_mirror(ppa_h, &srm_config);

// Après PPA
esp_cache_msync(image_buffer_, image_buffer_size_,
                ESP_CACHE_MSYNC_FLAG_DIR_M2C);
```

### Hypothèse 3: Alignement Mémoire

**Théorie:** Le PPA nécessite peut-être un alignement spécifique (64 bytes?).

**Test requis:**
```cpp
// Allouer avec alignement
this->image_buffer_ = (uint8_t*)heap_caps_aligned_alloc(64,
                        this->image_buffer_size_,
                        MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
```

### Hypothèse 4: M5Stack N'est Pas Plus Rapide

**Théorie:** Peut-être que M5Stack affiche aussi à ~20-22 FPS en réalité?

**Vérification requise:**
- Profiler le code M5Stack avec `esp_timer_get_time()`
- Mesurer le temps PPA exact dans leur implémentation

## 🎯 Tests à Effectuer (Ordre de Priorité)

**Status Update:** ✅ Tests 1-4 implémentés dans les commits suivants:
- Test 1 (mirror_x): Commit 5e4695d
- Test 2 (memory zones): Commit a89b43a
- Test 3 (cache sync): Commit ea48d5a
- Test 4 (alignment): Commit ed57dba

**Prochaine étape:** Tester sur hardware et analyser les logs pour voir l'impact.

---

### Test 1: Activer mirror_x (Rapide) ✅ IMPLÉMENTÉ

Changer `mirror_x = false` → `mirror_x = true` pour matcher M5Stack exactement.

**Probabilité de succès:** 10%
**Effort:** Minimal
**Status:** ✅ Implémenté (commit 5e4695d)

### Test 2: Vérifier les Adresses Mémoire (Rapide) ✅ IMPLÉMENTÉ

Logger les adresses des buffers V4L2 et image_buffer_ pour voir dans quelle zone mémoire ils sont.

**Probabilité de succès:** 30%
**Effort:** Minimal
**Status:** ✅ Implémenté (commit a89b43a) - Logs détaillés avec détection de zone mémoire

### Test 3: Cache Sync (Moyen) ✅ IMPLÉMENTÉ

Ajouter esp_cache_msync avant/après PPA.

**Probabilité de succès:** 40% ⭐ **PLUS HAUTE PROBABILITÉ**
**Effort:** Moyen
**Status:** ✅ Implémenté (commit ea48d5a) - Cache sync C2M avant PPA, M2C après

### Test 4: Alignement Mémoire (Moyen) ✅ IMPLÉMENTÉ

Utiliser heap_caps_aligned_alloc au lieu de heap_caps_malloc.

**Probabilité de succès:** 20%
**Effort:** Moyen
**Status:** ✅ Implémenté (commit ed57dba) - 64-byte alignment avec vérification

### Test 5: Mesurer M5Stack (Élevé) 🔜 À FAIRE

Modifier leur code pour profiler exactement le temps PPA.

**Probabilité de succès:** 100% (avoir la vérité)
**Effort:** Élevé

## 💡 Solution Alternative: Zero-Copy

Si le PPA reste lent après tous les tests, utiliser le zero-copy:

**Avantages:**
- Performance garantie: ~2ms au lieu de 43ms
- 30 FPS assuré

**Inconvénients:**
- Risque de tearing (LVGL lit pendant que driver écrit)
- Peut être acceptable en pratique

**Implémentation:** Retour au commit 108a4d3

## 📊 Profiling M5Stack Exact

Pour comparer pommes-à-pommes, il faudrait:

1. Modifier `hal_camera.cpp` pour ajouter:
```cpp
uint32_t t1 = esp_timer_get_time();
ppa_do_scale_rotate_mirror(ppa_srm_handle, &srm_config);
uint32_t t2 = esp_timer_get_time();
ESP_LOGI(TAG, "PPA time: %u us", (uint32_t)(t2-t1));
```

2. Compiler et tester

3. Comparer avec notre temps PPA (43488 us)

Cela nous dira définitivement si le PPA est censé être rapide ou non.
