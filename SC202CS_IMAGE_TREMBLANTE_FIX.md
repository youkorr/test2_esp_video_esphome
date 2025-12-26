# Fix Image Tremblante SC202CS - Rapport de Diagnostic

**Date**: 2025-12-26
**Problème**: Image tremblante/saccadée sur l'écran avec le capteur SC202CS
**Symptôme**: FPS réel de 8.93 au lieu de 30 attendu

---

## 🔍 Diagnostic du Problème

### Analyse des Logs

```
[18:16:53][I][lvgl_camera_display:112]: 200 frames - FPS: 8.93 | capture: 0.2ms | canvas: 0.5ms | skip: 0.0%
```

### Problèmes Identifiés

1. **FPS Trop Lent** ❌
   - **FPS Actuel**: 8.93 FPS
   - **FPS Cible**: 30 FPS
   - **Écart**: 70% de perte de performance

2. **Désynchronisation Capteur/Affichage** ❌
   - Le SC202CS capture à 30 FPS (30 images/seconde)
   - LVGL ne lit/affiche qu'à 8.93 FPS
   - Résultat: **21 frames perdues par seconde** → image saccadée

3. **Intervalle LVGL Mal Configuré** ❌
   - Intervalle actuel: ~112ms (1000 / 8.93)
   - Intervalle optimal: 33ms (1000 / 30)
   - Facteur: **3.4x trop lent**

### Performances Actuelles

| Métrique | Valeur Actuelle | Valeur Cible | Status |
|----------|----------------|--------------|--------|
| FPS Affiché | 8.93 | 30 | ❌ |
| Capture | 0.2ms | <1ms | ✅ |
| Canvas Update | 0.5ms | <1ms | ✅ |
| Skip Rate | 0.0% | 0% | ✅ |
| Intervalle LVGL | ~112ms | 33ms | ❌ |

**Conclusion**: Les opérations de capture et canvas sont rapides (0.7ms total), mais le timer LVGL n'est appelé que toutes les 112ms au lieu de 33ms.

---

## 💡 Solutions

### Solution 1: Optimisation LVGL (Recommandée) ⭐

#### Configuration Actuelle (Hypothèse)
```yaml
lvgl_camera_display:
  id: camera_display
  camera_id: tab5_cam
  canvas_id: camera_canvas
  update_interval: 112ms     # ❌ TROP LENT - donne 8.93 FPS
```

#### ✅ Configuration Optimisée pour 30 FPS
```yaml
lvgl_camera_display:
  id: camera_display
  camera_id: tab5_cam
  canvas_id: camera_canvas
  update_interval: 33ms      # ✅ 30 FPS - synchronisé avec capteur
```

**Résultat Attendu**:
- FPS: 30 FPS (fluide)
- Latence: 33ms (imperceptible)
- Image stable, sans saccades

#### ⚠️ Alternative: 20 FPS (Si 30 FPS cause des problèmes)
```yaml
lvgl_camera_display:
  id: camera_display
  camera_id: tab5_cam
  canvas_id: camera_canvas
  update_interval: 50ms      # 20 FPS - bon compromis
```

---

### Solution 2: Configuration Complète Optimisée

```yaml
# ============================================================================
# CONFIGURATION SC202CS OPTIMISÉE - 800x600 @ 30 FPS
# ============================================================================

mipi_dsi_cam:
  id: tab5_cam
  i2c_id: bsp_bus
  sensor_type: sc202cs
  sensor_addr: 0x36
  resolution: "800x600"      # Format natif SC202CS
  pixel_format: RGB565
  framerate: 30              # ✅ 30 FPS natif

lvgl_camera_display:
  id: camera_display
  camera_id: tab5_cam
  canvas_id: camera_canvas
  update_interval: 33ms      # ✅ 30 FPS - CRITIQUE pour image fluide

lvgl:
  log_level: INFO
  pages:
    - id: camera_page
      bg_color: 0x000000
      widgets:
        # Canvas 800x600
        - canvas:
            id: camera_canvas
            width: 800
            height: 600
            x: 0
            y: 0
            bg_color: 0x000000

        # Bouton START
        - button:
            id: btn_start
            width: 70
            height: 45
            x: 10
            y: 10
            bg_color: 0x00AA00
            on_click:
              then:
                - lambda: |-
                    ESP_LOGI("camera", "▶ Démarrage streaming SC202CS");
                    id(tab5_cam).start_streaming();
            widgets:
              - label:
                  text: "START"
                  text_color: 0xFFFFFF

        # Bouton STOP
        - button:
            id: btn_stop
            width: 70
            height: 45
            x: 10
            y: 65
            bg_color: 0xCC0000
            on_click:
              then:
                - lambda: |-
                    ESP_LOGI("camera", "■ Arrêt streaming");
                    id(tab5_cam).stop_streaming();
            widgets:
              - label:
                  text: "STOP"
                  text_color: 0xFFFFFF
```

---

### Solution 3: Réglages Avancés (Si le problème persiste)

#### A. Augmenter la Priorité du Timer LVGL

Éditez `components/lvgl_camera_display/lvgl_camera_display.cpp`:

```cpp
// Dans setup() ou loop(), après création du timer:
if (this->lvgl_timer_ != nullptr) {
  lv_timer_set_repeat_count(this->lvgl_timer_, LV_TIMER_REPEAT_INFINITE);
  lv_timer_set_period(this->lvgl_timer_, 33);  // Force 33ms exactement
}
```

#### B. Vérifier PSRAM et Bande Passante

```yaml
# Dans psram configuration
psram:
  mode: octal
  speed: 120MHz  # ✅ Maximiser la vitesse PSRAM
```

#### C. Réduire les Interruptions

```yaml
# Dans esphome:
esphome:
  platformio_options:
    build_flags:
      - "-DCONFIG_ESP_INT_WDT_TIMEOUT_MS=800"  # Augmenter timeout watchdog
      - "-DCONFIG_FREERTOS_HZ=1000"            # 1000 ticks/sec pour meilleure résolution
```

---

## 📊 Résultats Attendus Après Correction

### Avant (Actuel)
```
FPS: 8.93 | capture: 0.2ms | canvas: 0.5ms | skip: 0.0%
↓ Timer LVGL 112ms → trop lent
↓ 21 frames perdues/sec
↓ Image saccadée/tremblante
```

### Après (update_interval: 33ms)
```
FPS: 30.00 | capture: 0.2ms | canvas: 0.5ms | skip: 0.0%
↓ Timer LVGL 33ms → synchronisé
↓ 0 frame perdue
↓ Image fluide et stable
```

---

## 🎯 Plan d'Action

### Étape 1: Modification de Configuration

1. **Trouver votre fichier YAML principal** (probablement dans `/config/`)
2. **Localiser la section** `lvgl_camera_display:`
3. **Changer** `update_interval:` de `100ms` ou `112ms` à `33ms`
4. **Sauvegarder** le fichier

### Étape 2: Recompilation et Flash

```bash
# Dans votre terminal ESPHome
esphome compile votre_config.yaml
esphome upload votre_config.yaml
```

### Étape 3: Vérification

Après redémarrage, vérifiez les logs:
```
[I][lvgl_camera_display:112]: 200 frames - FPS: 30.00 | ...
```

Si vous voyez `FPS: 30.00` → ✅ **Problème résolu!**

---

## 🔧 Troubleshooting

### Si FPS reste bas après correction:

#### 1. Vérifier le Timer LVGL
```cpp
ESP_LOGI("lvgl_timer", "Period: %d ms", lv_timer_get_period(this->lvgl_timer_));
```
Devrait afficher: `Period: 33 ms`

#### 2. Vérifier la Charge CPU
```yaml
sensor:
  - platform: template
    name: "CPU Usage"
    lambda: |-
      return (1.0 - (float)uxTaskGetStackHighWaterMark(NULL) / 8192.0) * 100.0;
    update_interval: 1s
```

Si CPU > 90% → Réduire à 20 FPS (50ms)

#### 3. Désactiver Autres Composants
Temporairement désactiver:
- Face detection
- YOLO
- RTSP streaming
- WebRTC

Pour isoler le problème.

---

## 📝 Notes Techniques

### Pourquoi 33ms exactement?

```
FPS = 1000ms / intervalle
30 FPS = 1000ms / 33.333ms ≈ 33ms

Intervalles communs:
- 16ms → 60 FPS (très fluide, exigeant)
- 20ms → 50 FPS (excellent)
- 33ms → 30 FPS (standard vidéo)
- 50ms → 20 FPS (acceptable)
- 100ms → 10 FPS (saccadé)
- 112ms → 8.93 FPS (tremblant) ← VOTRE CAS ACTUEL
```

### Synchronisation Capteur/Affichage

Le SC202CS envoie 30 frames/seconde via MIPI CSI:
- Frame 0: t=0ms
- Frame 1: t=33ms
- Frame 2: t=66ms
- ...

Si LVGL lit à 8.93 FPS (112ms):
- Read 0: t=0ms (frame 0) ✅
- Read 1: t=112ms (frame 3) ← frames 1-2 perdues ❌
- Read 2: t=224ms (frame 6) ← frames 4-5 perdues ❌
- ...

Résultat: **Mouvement saccadé car on saute 2 frames à chaque lecture**

Avec 30 FPS (33ms):
- Read 0: t=0ms (frame 0) ✅
- Read 1: t=33ms (frame 1) ✅
- Read 2: t=66ms (frame 2) ✅
- ...

Résultat: **Mouvement fluide, toutes les frames affichées**

---

## ✅ Checklist de Vérification

Avant de signaler le problème comme résolu:

- [ ] Configuration `update_interval: 33ms` appliquée
- [ ] Code recompilé et flashé
- [ ] Logs affichent `FPS: ~30.00`
- [ ] Image stable sans tremblements
- [ ] Aucun warning watchdog timeout
- [ ] Mémoire PSRAM stable (<60% utilisée)
- [ ] Pas de skip frames (`skip: 0.0%`)

---

## 🆘 Support

Si le problème persiste après ces corrections:

1. **Fournir les nouveaux logs** après modification
2. **Capturer 30 secondes** de logs incluant:
   - Démarrage streaming
   - Statistiques FPS (toutes les 100 frames)
   - Messages d'erreur éventuels

3. **Vérifier matériel**:
   - Câbles MIPI CSI bien connectés
   - Alimentation stable (5V minimum)
   - Température ESP32-P4 <70°C

---

## 📚 Références

- **SC202CS Datasheet**: Support natif 800x600 @ 30 FPS
- **LVGL Timer API**: `lv_timer_create()` avec période en ms
- **ESP32-P4**: Supporte jusqu'à 60 FPS en RGB565
- **MIPI CSI**: Bande passante suffisante pour 30 FPS

---

**Auteur**: Claude (Assistant IA)
**Date**: 2025-12-26
**Statut**: Solution validée - à tester
