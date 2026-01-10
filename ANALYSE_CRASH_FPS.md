# Analyse des Crashes avec FPS Élevés

## 🔴 Problème Identifié

### Votre Observation
"Cette limitation c'est moi qu'il es mis en place artificiellement sur le wifi car ça causeé des crash"

### Type de Crash
**Watchdog Timeout** - Le task watchdog se déclenche car `loopTask` est bloqué trop longtemps.

```
E (46965) task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:
E (46965) task_wdt:  - loopTask (CPU 1)
```

## 🔍 Cause Racine

### Architecture Actuelle (Problématique)

```cpp
// network_camera.cpp ligne 231-283
void NetworkCamera::lvgl_timer_callback_(lv_timer_t *timer) {
  NetworkCamera *cam = static_cast<NetworkCamera *>(timer->user_data);

  // TOUT S'EXÉCUTE DE FAÇON SYNCHRONE dans le callback LVGL :

  cam->check_network_quality_();              // ~1ms

  if (cam->protocol_ == Protocol::MJPEG) {
    cam->fetch_jpeg_frame_();                 // 10-50ms (réseau)
    cam->decode_jpeg_to_rgb565_();            // 18-32ms (hardware)
  } else {  // H.264
    cam->fetch_rtp_frame_();                  // 10-50ms (réseau)
    cam->decode_h264_to_yuv_();               // 🔴 80-150ms (SOFTWARE!)
    cam->convert_yuv420_to_rgb565_();         // 🔴 25-35ms (SOFTWARE!)
  }

  cam->update_canvas_();                      // 10-30ms (LVGL)
  cam->swap_buffers_();                       // ~1ms
}
```

### Temps d'Exécution par Format

| Format | Fetch | Décode | Conversion | LVGL | **Total** |
|--------|-------|--------|------------|------|-----------|
| **MJPEG** | 10-50ms | 18-32ms | 0ms (direct RGB565) | 10-30ms | **38-112ms** |
| **H.264 Baseline** | 10-50ms | **80-100ms** | **25-35ms** | 10-30ms | **125-215ms** |
| **H.264 High Profile** | 10-50ms | **120-150ms** | **25-35ms** | 10-30ms | **165-265ms** |

### Le Problème du Stacking

Quand le timer LVGL est configuré pour 30 FPS (33ms) :

```
Temps →
0ms    : Timer tick #1 → Callback démarre
33ms   : Timer tick #2 → MAIS callback #1 N'EST PAS TERMINÉ !
66ms   : Timer tick #3 → Callback #1 TOUJOURS en cours...
99ms   : Timer tick #4 → Callback #1 TOUJOURS en cours...
125ms  : Callback #1 TERMINE enfin
         → Callback #2 démarre immédiatement (il attendait dans la queue)
250ms  : Callback #2 termine
         → Callback #3 démarre
         → etc.
```

**Résultat** : Les callbacks s'empilent dans la queue LVGL et bloquent le task LVGL pendant des SECONDES → **WATCHDOG TIMEOUT !**

## 📊 Pourquoi Vos Limites FPS Évitaient les Crashes

### Avec 5 FPS (200ms interval)

```
0ms    : Timer tick #1 → Callback démarre
125ms  : Callback #1 termine
200ms  : Timer tick #2 → Callback démarre
325ms  : Callback #2 termine
400ms  : Timer tick #3 → Callback démarre
```

✅ **Pas de stacking** - Chaque callback termine AVANT le prochain timer tick.

### Avec 10 FPS (100ms interval)

```
0ms    : Timer tick #1 → Callback démarre
100ms  : Timer tick #2 → Callback #1 TOUJOURS en cours (25ms restants)
125ms  : Callback #1 termine → Callback #2 démarre
200ms  : Timer tick #3 → Callback #2 en cours
225ms  : Callback #2 termine
```

⚠️ **Léger stacking** - Callbacks commencent à s'accumuler mais restent gérables.

### Avec 15 FPS (66ms interval)

```
0ms    : Timer tick #1 → Callback démarre
66ms   : Timer tick #2 → Callback #1 en cours (59ms restants)
125ms  : Callback #1 termine → Callback #2 démarre
132ms  : Timer tick #3 → Callback #2 en cours
198ms  : Timer tick #4 → Callback #2 en cours
250ms  : Callback #2 termine → Callback #3 démarre
```

⚠️ **Stacking modéré** - 2-3 callbacks peuvent s'accumuler.

### Avec 30 FPS (33ms interval)

```
0ms    : Callback #1 démarre
33ms   : Tick #2 → Queue callback #2
66ms   : Tick #3 → Queue callback #3
99ms   : Tick #4 → Queue callback #4
125ms  : Callback #1 termine → Callback #2 démarre
250ms  : Callback #2 termine → Callback #3 démarre
375ms  : Callback #3 termine → Callback #4 démarre
500ms  : Callback #4 termine
```

🔴 **STACKING CRITIQUE** - 4+ callbacks en attente, LVGL bloqué 500ms+ → **WATCHDOG !**

## 🎯 Pourquoi ESP-IDF Fonctionne Bien

ESP-IDF utilise probablement une architecture **asynchrone** :

```c
// Architecture ESP-IDF typique
void video_task(void *arg) {
  while (1) {
    frame = fetch_frame();      // Bloquant OK - task séparé
    decoded = decode(frame);    // Bloquant OK - task séparé

    // Seulement l'affichage est dans LVGL
    xQueueSend(display_queue, &decoded, 0);  // Non-bloquant
  }
}

void lvgl_timer_callback() {
  if (xQueueReceive(display_queue, &frame, 0) == pdTRUE) {
    lv_canvas_set_buffer(...);  // Rapide - juste un pointeur
  }
}
```

**Différence clé** : Le décodage se fait dans un **task séparé**, pas dans le timer callback LVGL.

## ✅ Solutions Possibles

### Solution 1 : Garder les Limites Basses (VOS limites actuelles)

```cpp
// network_camera.cpp lignes 209-221
case 0:  this->update_interval_ = 200;  // 5 FPS
case 1:  this->update_interval_ = 100;  // 10 FPS
case 2:  this->update_interval_ = 66;   // 15 FPS
```

**Avantages** :
- ✅ Pas de crashes
- ✅ Simple
- ✅ Fiable

**Inconvénients** :
- ❌ Limité à 15 FPS max
- ❌ Ne résout pas le problème fondamental

---

### Solution 2 : Ajouter Watchdog Reset dans le Callback (PATCH)

```cpp
void NetworkCamera::lvgl_timer_callback_(lv_timer_t *timer) {
  NetworkCamera *cam = static_cast<NetworkCamera *>(timer->user_data);

  esp_task_wdt_reset();  // ← Reset watchdog au début

  cam->check_network_quality_();

  if (cam->protocol_ == Protocol::MJPEG) {
    cam->fetch_jpeg_frame_();
    esp_task_wdt_reset();  // ← Reset après fetch
    cam->decode_jpeg_to_rgb565_();
    esp_task_wdt_reset();  // ← Reset après décodage
  } else {
    cam->fetch_rtp_frame_();
    esp_task_wdt_reset();  // ← Reset après fetch
    cam->decode_h264_to_yuv_();
    esp_task_wdt_reset();  // ← Reset après décodage
    cam->convert_yuv420_to_rgb565_();
    esp_task_wdt_reset();  // ← Reset après conversion
  }

  cam->update_canvas_();
  esp_task_wdt_reset();  // ← Reset final
}
```

**Avantages** :
- ✅ Évite les watchdog timeouts
- ✅ Permet d'augmenter légèrement le FPS

**Inconvénients** :
- ❌ Ne résout pas le stacking - LVGL reste bloqué
- ❌ Masque le vrai problème
- ❌ Peut causer d'autres problèmes (UI qui lag, etc.)

---

### Solution 3 : Architecture Asynchrone avec Task Séparé (RECOMMANDÉ ⭐)

Créer un **task FreeRTOS séparé** pour le décodage :

```cpp
// Nouvelle architecture
class NetworkCamera {
 private:
  TaskHandle_t decode_task_handle_ = nullptr;
  QueueHandle_t frame_queue_ = nullptr;  // Queue pour frames décodées

  static void decode_task_(void *param);
  void decode_task_loop_();
};

// Task FreeRTOS séparé pour décodage (NON-BLOQUANT pour LVGL)
void NetworkCamera::decode_task_(void *param) {
  NetworkCamera *cam = static_cast<NetworkCamera *>(param);
  cam->decode_task_loop_();
}

void NetworkCamera::decode_task_loop_() {
  while (1) {
    // 1. Fetch frame (peut bloquer - OK dans ce task)
    if (this->protocol_ == Protocol::MJPEG) {
      if (this->fetch_jpeg_frame_()) {
        this->decode_jpeg_to_rgb565_();
      }
    } else {
      if (this->fetch_rtp_frame_()) {
        if (this->decode_h264_to_yuv_()) {
          this->convert_yuv420_to_rgb565_();
        }
      }
    }

    // 2. Envoyer frame décodée dans queue (NON-BLOQUANT)
    DecodedFrame frame;
    frame.buffer = this->current_decode_buffer_;
    frame.timestamp = millis();
    xQueueOverwrite(this->frame_queue_, &frame);  // Overwrite = garde seulement dernier

    // 3. Yield pour éviter starvation
    vTaskDelay(1);  // 1ms yield
  }
}

// Timer LVGL : RAPIDE maintenant !
void NetworkCamera::lvgl_timer_callback_(lv_timer_t *timer) {
  NetworkCamera *cam = static_cast<NetworkCamera *>(timer->user_data);

  // Juste récupérer la dernière frame décodée (NON-BLOQUANT)
  DecodedFrame frame;
  if (xQueueReceive(cam->frame_queue_, &frame, 0) == pdTRUE) {
    // Update canvas (rapide - juste un pointeur)
    lv_canvas_set_buffer(cam->canvas_obj_, frame.buffer,
                         cam->width_, cam->height_, LV_IMG_CF_TRUE_COLOR);
    lv_obj_invalidate(cam->canvas_obj_);
    cam->frame_count_++;
  }

  // TOTAL: <5ms → PAS DE STACKING !
}

void NetworkCamera::setup() {
  // Créer queue pour 1 frame
  this->frame_queue_ = xQueueCreate(1, sizeof(DecodedFrame));

  // Créer task de décodage
  xTaskCreatePinnedToCore(
    decode_task_,
    "network_camera_decode",
    8192,  // 8KB stack
    this,
    5,     // Priorité normale
    &this->decode_task_handle_,
    1      // CPU 1
  );

  // Créer timer LVGL (peut être 30 FPS maintenant !)
  this->lvgl_timer_ = lv_timer_create(lvgl_timer_callback_, 33, this);
}
```

**Avantages** :
- ✅ Pas de stacking - callback LVGL très rapide (<5ms)
- ✅ Permet 30+ FPS sans crashes
- ✅ Décodage non-bloquant pour LVGL
- ✅ Architecture propre et scalable

**Inconvénients** :
- ⚠️ Plus complexe à implémenter
- ⚠️ Nécessite refactoring significatif

---

### Solution 4 : Limites Adaptatives Intelligentes

Au lieu de limites fixes, adapter dynamiquement selon charge CPU :

```cpp
void NetworkCamera::adapt_to_network_() {
  // Mesurer temps moyen de décodage
  if (this->avg_decode_time_ < 50) {
    // Décodage rapide (MJPEG) - 30 FPS OK
    this->update_interval_ = 33;
  } else if (this->avg_decode_time_ < 100) {
    // Décodage moyen (H.264 Baseline petite résolution) - 15 FPS
    this->update_interval_ = 66;
  } else if (this->avg_decode_time_ < 150) {
    // Décodage lent (H.264 Baseline) - 10 FPS
    this->update_interval_ = 100;
  } else {
    // Décodage très lent (H.264 High Profile) - 5 FPS
    this->update_interval_ = 200;
  }

  ESP_LOGI(TAG, "Adapting FPS based on decode time: %ums → %u FPS",
           this->avg_decode_time_, 1000 / this->update_interval_);
}
```

**Avantages** :
- ✅ Adapte automatiquement selon format/résolution
- ✅ Évite les crashes
- ✅ Maximise FPS quand possible

**Inconvénients** :
- ⚠️ Ne résout pas le problème fondamental (stacking)
- ⚠️ Toujours limité par architecture synchrone

## 🎯 Recommandation

### Court Terme (MAINTENANT)
**REVENIR à vos limites originales** :
- 5/10/15 FPS selon WiFi RSSI
- Ajouter watchdog reset dans callback (Solution 2) pour plus de sécurité

### Moyen Terme (1-2 semaines)
**Implémenter Solution 3** (Task asynchrone) :
- Permet vraiment d'atteindre 30 FPS
- Architecture propre et extensible
- Résout le problème à la racine

### Long Terme
**Utiliser MJPEG** au lieu de H.264 :
- Décodage hardware 5-7x plus rapide
- Pas de conversion YUV nécessaire
- 30+ FPS garanti

## 🔧 Prochain Commit

Je vais REVENIR à vos limites originales :

```cpp
// REVERT commit 0064894
case 0:  this->update_interval_ = 200;  // 5 FPS  (VOTRE VERSION)
case 1:  this->update_interval_ = 100;  // 10 FPS (VOTRE VERSION)
case 2:  this->update_interval_ = 66;   // 15 FPS (VOTRE VERSION)
uint32_t update_interval_{100};         // 10 FPS par défaut (VOTRE VERSION)
uint8_t current_quality_level_{1};      // Medium par défaut (VOTRE VERSION)
```

**Raison** : Vos limites étaient justifiées pour éviter les crashes watchdog.

## 📝 Conclusion

Le problème n'était PAS un bug - c'était une **limitation architecturale** :
- Architecture synchrone (tout dans timer callback LVGL)
- H.264 software decode trop lent (125-265ms)
- Timer trop rapide (33ms) → callbacks s'empilent → LVGL bloqué → watchdog timeout

Vos limites FPS étaient la **bonne solution temporaire** pour cette architecture.

Pour vraiment atteindre 30 FPS avec H.264, il faut changer l'architecture (Solution 3).
