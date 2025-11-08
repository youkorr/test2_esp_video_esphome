# Guide: Profiler le PPA de M5Stack Tab5

## 🎯 Objectif

Mesurer le temps réel que prend le PPA dans le code M5Stack Tab5 pour déterminer si:
- 43ms est normal pour le PPA sur ESP32-P4
- M5Stack obtient réellement 30+ FPS ou aussi ~20 FPS

## 📁 Fichier à Modifier

**Fichier:** `platforms/tab5/main/hal/components/hal_camera.cpp`
**Fonction:** `app_camera_display`

## ✏️ Modifications à Apporter

### 1. Ajouter l'include pour le timer

En haut du fichier, ajouter:

```cpp
#include "esp_timer.h"
```

### 2. Ajouter le profiling dans la boucle

Trouver la section de la boucle `while(1)` qui contient:

```cpp
while (1) {
    memset(&buf, 0, sizeof(buf));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = MEMORY_TYPE;
    if (ioctl(camera->fd, VIDIOC_DQBUF, &buf) != 0) {
        ESP_LOGE(TAG, "failed to receive video frame");
        break;
    }

    ppa_srm_oper_config_t srm_config = {
        .in = {.buffer = camera->buffer[buf.index],
               .pic_w = 1280, .pic_h = 720, ...},
        .out = {.buffer = img_show_data,
                .buffer_size = img_show_size, ...},
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = 1, .scale_y = 1,
        .mirror_x = true, .mirror_y = false, ...
    };
    ppa_do_scale_rotate_mirror(ppa_srm_handle, &srm_config);
```

**Remplacer par:**

```cpp
while (1) {
    // Variables de profiling statiques
    static uint32_t frame_count = 0;
    static uint32_t total_dqbuf_us = 0;
    static uint32_t total_ppa_us = 0;
    static uint32_t total_qbuf_us = 0;
    static uint32_t total_loop_us = 0;

    uint32_t loop_start = esp_timer_get_time();

    memset(&buf, 0, sizeof(buf));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = MEMORY_TYPE;

    uint32_t t1 = esp_timer_get_time();
    if (ioctl(camera->fd, VIDIOC_DQBUF, &buf) != 0) {
        ESP_LOGE(TAG, "failed to receive video frame");
        break;
    }
    uint32_t t2 = esp_timer_get_time();

    ppa_srm_oper_config_t srm_config = {
        .in = {.buffer = camera->buffer[buf.index],
               .pic_w = 1280, .pic_h = 720,
               .block_w = 1280, .block_h = 720,
               .block_offset_x = 0, .block_offset_y = 0,
               .srm_cm = PPA_SRM_COLOR_MODE_RGB565},
        .out = {.buffer = img_show_data,
                .buffer_size = img_show_size,
                .pic_w = 1280, .pic_h = 720,
                .block_offset_x = 0, .block_offset_y = 0,
                .srm_cm = PPA_SRM_COLOR_MODE_RGB565},
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = 1, .scale_y = 1,
        .mirror_x = true, .mirror_y = false,
        .rgb_swap = false, .byte_swap = false,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    // ⭐ MESURE DU TEMPS PPA ⭐
    ppa_do_scale_rotate_mirror(ppa_srm_handle, &srm_config);
    uint32_t t3 = esp_timer_get_time();

    // Re-queue buffer
    if (ioctl(camera->fd, VIDIOC_QBUF, &buf) != 0) {
        ESP_LOGE(TAG, "failed to queue video buffer");
        break;
    }
    uint32_t t4 = esp_timer_get_time();

    uint32_t loop_end = esp_timer_get_time();

    // Accumulation
    frame_count++;
    total_dqbuf_us += (t2 - t1);
    total_ppa_us += (t3 - t2);
    total_qbuf_us += (t4 - t3);
    total_loop_us += (loop_end - loop_start);

    // Log toutes les 100 frames
    if (frame_count % 100 == 0) {
        float avg_dqbuf = total_dqbuf_us / 100.0f;
        float avg_ppa = total_ppa_us / 100.0f;
        float avg_qbuf = total_qbuf_us / 100.0f;
        float avg_loop = total_loop_us / 100.0f;
        float fps = 100000000.0f / total_loop_us;  // us -> FPS

        ESP_LOGI(TAG, "📊 M5Stack Profiling (avg over 100 frames):");
        ESP_LOGI(TAG, "   DQBUF: %.0f us", avg_dqbuf);
        ESP_LOGI(TAG, "   PPA copy: %.0f us (%.1f ms) ← KEY METRIC", avg_ppa, avg_ppa / 1000.0f);
        ESP_LOGI(TAG, "   QBUF: %.0f us", avg_qbuf);
        ESP_LOGI(TAG, "   Total loop: %.0f us (%.1f ms)", avg_loop, avg_loop / 1000.0f);
        ESP_LOGI(TAG, "   FPS: %.2f", fps);
        ESP_LOGI(TAG, "   PPA Bandwidth: %.1f MB/s", (1843200.0f / avg_ppa));

        // Reset counters
        total_dqbuf_us = 0;
        total_ppa_us = 0;
        total_qbuf_us = 0;
        total_loop_us = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(10));  // Leur délai habituel
}
```

## 🔧 Compilation et Test

1. **Cloner le repo M5Stack:**
   ```bash
   git clone https://github.com/m5stack/M5Tab5-UserDemo.git
   cd M5Tab5-UserDemo
   ```

2. **Appliquer la modification** dans `platforms/tab5/main/hal/components/hal_camera.cpp`

3. **Compiler:**
   ```bash
   idf.py build
   ```

4. **Flasher:**
   ```bash
   idf.py -p /dev/ttyUSB0 flash monitor
   ```

5. **Lancer la démo caméra** et observer les logs

## 📊 Résultats Attendus

### Scénario 1: PPA Rapide chez M5Stack
```
📊 M5Stack Profiling (avg over 100 frames):
   DQBUF: 400 us
   PPA copy: 15000 us (15.0 ms) ← Beaucoup plus rapide!
   QBUF: 50 us
   Total loop: 25000 us (25.0 ms)
   FPS: 40.00
   PPA Bandwidth: 123 MB/s
```

**Interprétation:** Il y a quelque chose dans notre code qui ralentit le PPA → chercher la différence

### Scénario 2: PPA Lent aussi chez M5Stack (ATTENDU)
```
📊 M5Stack Profiling (avg over 100 frames):
   DQBUF: 400 us
   PPA copy: 43000 us (43.0 ms) ← Pareil que nous!
   QBUF: 50 us
   Total loop: 53000 us (53.0 ms)
   FPS: 18.87
   PPA Bandwidth: 42.8 MB/s
```

**Interprétation:** 43ms est la performance normale du PPA → zero-copy est la solution

### Scénario 3: Pas de PPA (Surprenant)
```
📊 M5Stack Profiling (avg over 100 frames):
   DQBUF: 400 us
   PPA copy: 2000 us (2.0 ms) ← Très rapide!
   QBUF: 50 us
   Total loop: 12000 us (12.0 ms)
   FPS: 83.33
```

**Interprétation:** M5Stack n'utilise peut-être pas le PPA comme on le pense

## 🎯 Comparaison avec Notre Code

### Notre Performance Actuelle:
```
📊 Profiling (avg over 100 frames):
   DQBUF: 396 us
   PPA copy: 43492 us (43.5 ms)
   QBUF: 54 us
   TOTAL: 43942 us (43.9 ms) → ~22 FPS
   PPA Bandwidth: 42.4 MB/s
```

### Points de Comparaison:
1. **Temps PPA** - Si M5Stack = 43ms aussi → C'est normal
2. **FPS réel** - Si M5Stack = ~20 FPS aussi → Notre perception était fausse
3. **Bandwidth** - Si M5Stack = ~42 MB/s → Limite hardware du PPA

## 💡 Décision Basée sur les Résultats

### Si M5Stack PPA = ~43ms:
✅ **Implémenter zero-copy** (commit 108a4d3)
- Garantit 30 FPS
- Le PPA est juste lent pour 720p RGB565

### Si M5Stack PPA = <20ms:
🔍 **Chercher la différence** dans:
- Configuration PPA exacte
- Flags de compilation
- Version du driver ESP-Video
- Configuration SPIRAM

### Si M5Stack n'utilise pas le PPA:
🤔 **Revoir l'architecture** - Ils utilisent peut-être:
- Zero-copy direct
- DMA différent
- ISP direct-to-display

## 📚 Informations Supplémentaires

**Taille des données:** 1280 × 720 × 2 bytes (RGB565) = 1,843,200 bytes

**Bandwidth PPA théorique:**
- SPIRAM: 80-120 MB/s typique sur ESP32-P4
- Notre mesure: 42 MB/s → Environ 50% de la bande passante max

**Hypothèse:**
Le PPA avec `PPA_TRANS_MODE_BLOCKING` et SPIRAM→SPIRAM copy pourrait être limité à ~40-45 MB/s, donnant 43ms pour 1.8MB.

---

**Prochaine étape:** Tester M5Stack avec ces modifications et comparer les résultats.
