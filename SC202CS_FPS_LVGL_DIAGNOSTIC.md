# SC202CS - Diagnostic FPS Ralenti LVGL (8.93 au lieu de 30)

**Date**: 2025-12-26
**Problème**: `update_interval: 33ms` configuré mais FPS réel = 8.93
**Cause**: LVGL timer ralenti malgré configuration correcte

---

## 🔍 Analyse du Problème

### Configuration vs Réalité

| Paramètre | Configuré | Réel | Status |
|-----------|-----------|------|--------|
| `update_interval` | 33ms | ~112ms | ❌ 3.4x ralenti |
| FPS Cible | 30 | 8.93 | ❌ |
| Capture | - | 0.2ms | ✅ |
| Canvas | - | 0.5ms | ✅ |

**Conclusion**: Le timer LVGL **existe** avec 33ms, mais **n'est pas exécuté** à cette fréquence.

### Causes Probables

1. **LVGL Display Refresh Rate Limitée** (PRINCIPAL SUSPECT)
   - Le display RGB/MIPI DSI peut avoir un `update_interval` propre
   - LVGL peut avoir `auto_clear_enabled: true` qui ralentit
   - Buffer flushing peut bloquer

2. **LVGL Task Handler Surchargé**
   - `lv_task_handler()` ou `lv_timer_handler()` appelé trop lentement
   - Autres tâches LVGL qui monopolisent le CPU

3. **Display Driver Bloquant**
   - Flush callback qui attend trop longtemps
   - DMA transfer qui bloque

---

## 💡 Solutions par Ordre de Probabilité

### Solution 1: Vérifier Configuration Display (PRINCIPAL)

Le display RGB/MIPI DSI a probablement un paramètre qui limite le refresh rate.

#### Trouver votre configuration display

```bash
# Dans votre terminal
grep -r "display:" *.yaml --include="*.yaml"
grep -r "auto_clear_enabled\|update_interval" *.yaml
```

#### ✅ Configuration Display Optimisée

```yaml
display:
  - platform: rgb_lcd  # ou votre plateforme display
    id: main_display
    update_interval: 16ms    # ← CRITIQUE: 60 FPS max pour laisser marge
    auto_clear_enabled: false  # ← Important: évite clear inutile
    # ... autres paramètres ...
```

**Explications**:
- `update_interval: 16ms` → 60 FPS max (donne marge à LVGL pour 30 FPS)
- `auto_clear_enabled: false` → évite un clear screen coûteux à chaque frame

---

### Solution 2: Augmenter Priorité Timer LVGL (CODE)

Modifier `components/lvgl_camera_display/lvgl_camera_display.cpp`:

#### A. Ajouter Logs de Diagnostic

```cpp
// Dans lvgl_timer_callback_ (ligne ~62)
void LVGLCameraDisplay::lvgl_timer_callback_(lv_timer_t *timer) {
  static uint32_t last_call = 0;
  uint32_t now = millis();
  uint32_t delta = now - last_call;

  if (delta > 50) {  // Si > 50ms, logger
    ESP_LOGW(TAG, "⚠️  Timer ralenti: %ums depuis dernier appel (cible: 33ms)", delta);
  }

  last_call = now;

  LVGLCameraDisplay *display = static_cast<LVGLCameraDisplay *>(timer->user_data);
  if (display != nullptr) {
    display->update_camera_frame_();
  }
}
```

**Cela vous dira**:
- Si le timer est vraiment ralenti
- De combien il est ralenti
- Si c'est LVGL qui ne l'appelle pas

#### B. Forcer Période Timer

```cpp
// Dans loop() après création timer (ligne ~44)
if (this->enabled_ && this->lvgl_timer_ == nullptr) {
  ESP_LOGI(TAG, "Starting LVGL Camera Display...");
  this->lvgl_timer_ = lv_timer_create(lvgl_timer_callback_, this->update_interval_, this);
  if (this->lvgl_timer_ != nullptr) {
    // ✅ FORCER période exacte
    lv_timer_set_period(this->lvgl_timer_, 33);
    lv_timer_set_repeat_count(this->lvgl_timer_, LV_TIMER_REPEAT_INFINITE);

    uint32_t period = lv_timer_get_period(this->lvgl_timer_);
    ESP_LOGI(TAG, "✓ Timer créé: période=%ums", period);

    ESP_LOGI(TAG, "LVGL Camera Display started");
  }
}
```

---

### Solution 3: Désactiver V-Sync / Buffer Swap

Si votre display utilise un double-buffering avec V-Sync, cela peut limiter à 60 FPS / 2 = 30 FPS théorique, mais avec overhead → ~9 FPS effectif.

```yaml
display:
  - platform: rgb_lcd
    # ...
    # Chercher et désactiver:
    # swap_xy: false
    # mirror_x: false
    # mirror_y: false
```

---

### Solution 4: Augmenter Fréquence FreeRTOS

Le scheduler FreeRTOS peut limiter la précision des timers.

```yaml
esphome:
  platformio_options:
    build_flags:
      - "-DCONFIG_FREERTOS_HZ=1000"  # 1000 ticks/sec au lieu de 100
      - "-DCONFIG_ESP_TIMER_TASK_STACK_SIZE=4096"
```

**Effet**: Timer LVGL aura une résolution de 1ms au lieu de 10ms.

---

### Solution 5: Vérifier Charge LVGL

LVGL peut être surchargé par d'autres widgets/animations.

#### Désactiver Temporairement

```yaml
# Commentez temporairement dans votre YAML:
# - face_detection
# - animations LVGL
# - autres widgets complexes
```

#### Mesurer Charge LVGL

Ajoutez dans votre code:

```cpp
// Dans setup()
lv_log_register_print_cb([](const char *buf) {
  ESP_LOGD("lvgl", "%s", buf);
});
```

---

## 🎯 Plan d'Action Immédiat

### Étape 1: Diagnostic (5 min)

**Ajoutez des logs dans votre YAML principal**:

```yaml
esphome:
  on_loop:
    - lambda: |-
        static uint32_t last_log = 0;
        if (millis() - last_log > 5000) {  // Toutes les 5 secondes
          auto timer = id(camera_display).get_timer();
          if (timer != nullptr) {
            uint32_t period = lv_timer_get_period(timer);
            ESP_LOGI("diagnostic", "Timer période: %u ms", period);
          }
          last_log = millis();
        }
```

**Résultat attendu**:
- Si période = 33ms → Timer OK, problème ailleurs
- Si période ≠ 33ms → Timer modifié par LVGL

### Étape 2: Trouver Configuration Display

```bash
# Cherchez dans vos YAML:
grep -r "display:" *.yaml
grep -r "update_interval" *.yaml | grep -v camera
```

**Cherchez**:
- `update_interval:` dans section `display:` (pas `lvgl_camera_display:`)
- `auto_clear_enabled:`
- `buffer_size:`

### Étape 3: Modifier Configuration Display

```yaml
display:
  - platform: rgb_lcd  # Ou votre plateforme
    id: main_display
    update_interval: 16ms      # ✅ 60 FPS max
    auto_clear_enabled: false  # ✅ Pas de clear inutile
```

### Étape 4: Recompiler et Tester

```bash
esphome compile votre_config.yaml
esphome upload votre_config.yaml
```

### Étape 5: Vérifier Logs

Vous devriez voir:
```
[I][lvgl_camera_display:112]: 200 frames - FPS: 30.00 | ...
```

---

## 🔧 Solution Temporaire (Workaround)

Si rien ne fonctionne, **réduisez temporairement la résolution**:

```yaml
mipi_dsi_cam:
  id: tab5_cam
  resolution: "640x480"  # ← VGA au lieu de 800x600
  # Moins de pixels = traitement plus rapide
```

Cela devrait améliorer le FPS même si le timer est ralenti.

---

## 📊 Test de Performance LVGL

Créez un test simple pour isoler le problème:

```yaml
# Test minimal - désactivez TOUT sauf ceci:
lvgl:
  log_level: DEBUG  # ← IMPORTANT pour voir les logs LVGL
  pages:
    - id: test_page
      widgets:
        - label:
            id: fps_label
            text: "FPS: 0"
            x: 10
            y: 10

# Script de test FPS
interval:
  - interval: 33ms  # Timer ESPHome indépendant
    then:
      - lambda: |-
          static uint32_t count = 0;
          static uint32_t last_time = 0;
          count++;

          if (millis() - last_time > 1000) {
            float fps = count * 1000.0 / (millis() - last_time);
            ESP_LOGI("test", "FPS ESPHome timer: %.2f", fps);
            lv_label_set_text_fmt(id(fps_label), "FPS: %.1f", fps);

            count = 0;
            last_time = millis();
          }
```

**Si ce test donne ~30 FPS**:
- ✅ Le problème est dans `lvgl_camera_display`
- ✅ LVGL lui-même fonctionne correctement

**Si ce test donne ~9 FPS**:
- ❌ LVGL est globalement ralenti
- ❌ Problème avec display ou LVGL core

---

## 🆘 Si Rien ne Fonctionne

### Option A: Passer en Mode Polling

Au lieu d'utiliser le timer LVGL, capturer dans `loop()`:

```cpp
// Dans esp_cam_sensor_camera.cpp
void loop() {
  static uint32_t last_capture = 0;
  if (millis() - last_capture >= 33) {  // 30 FPS
    if (this->is_streaming()) {
      this->capture_frame();
      this->update_canvas_();
    }
    last_capture = millis();
  }
}
```

### Option B: Utiliser Interrupt Timer

Créer un timer hardware ESP32:

```cpp
// Timer hardware à 30 Hz
hw_timer_t *timer = timerBegin(0, 80, true);  // 1 MHz
timerAttachInterrupt(timer, &onTimer, true);
timerAlarmWrite(timer, 33333, true);  // 33.333ms = 30 Hz
timerAlarmEnable(timer);
```

---

## 📝 Checklist de Vérification

- [ ] Trouver configuration `display:` actuelle
- [ ] Vérifier `update_interval` du display (pas camera)
- [ ] Ajouter `auto_clear_enabled: false`
- [ ] Mettre `update_interval: 16ms` sur display
- [ ] Ajouter logs diagnostic timer
- [ ] Recompiler et flasher
- [ ] Vérifier nouveaux logs FPS
- [ ] Si FPS OK → problème résolu
- [ ] Si FPS toujours bas → tester workarounds

---

## 🎓 Explication Technique

### Pourquoi le Display update_interval affecte LVGL?

```
Display Driver (16ms)
    ↓
LVGL Task Handler appelé par display
    ↓
LVGL Timers exécutés (33ms configuré)
    ↓
Mais limité par fréquence d'appel du handler
```

Si Display = 112ms:
- Handler appelé toutes les 112ms
- Timer LVGL (33ms) ne peut s'exécuter QUE quand handler est appelé
- Résultat: Timer effectif = 112ms même si configuré à 33ms

**Solution**: Display update_interval < LVGL timer period

---

**Auteur**: Claude (Assistant IA)
**Date**: 2025-12-26
**Priorité**: HAUTE - Image tremblante affecte UX
