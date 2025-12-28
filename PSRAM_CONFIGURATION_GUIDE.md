# 💾 Guide Configuration PSRAM - ESP32-P4 Multi-Détection

## 🎯 Solution pour Exécuter Plusieurs Détections Ensemble

**Problème identifié** : Votre ESP32-P4 manque de RAM pour 3 détections (>15 Mo utilisés / 16 Mo disponibles)

**Solution trouvée** : Waveshare utilise **PSRAM externe** (SPIRAM) pour augmenter la mémoire disponible !

---

## 🔍 Étape 1 : Vérifier votre PSRAM

### Test Rapide

Ajoutez ceci à votre configuration ESPHome :

```yaml
# À ajouter temporairement dans votre config
esphome:
  on_boot:
    priority: -100
    then:
      - lambda: |-
          size_t psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
          if (psram > 0) {
            ESP_LOGI("psram", "✅ PSRAM: %.2f MB disponibles", psram / (1024.0 * 1024.0));
          } else {
            ESP_LOGE("psram", "❌ PAS DE PSRAM - Limité à 1 détection");
          }
```

### Logs Attendus

**Si PSRAM présente** :
```
[psram] ✅ PSRAM: 16.00 MB disponibles
[psram] 🎉 Vous pouvez utiliser plusieurs détections !
```

**Si pas de PSRAM** :
```
[psram] ❌ PAS DE PSRAM - Limité à 1 détection
```

---

## ⚙️ Étape 2 : Activer PSRAM dans ESPHome

### Configuration Minimale

```yaml
esphome:
  name: esp32-p4-camera
  platformio_options:
    board_build.arduino.memory_type: opi_opi  # ESP32-P4 avec PSRAM
    board_build.f_flash: 80000000L
    board_build.flash_mode: qio
    build_flags:
      # Activer PSRAM
      - -DBOARD_HAS_PSRAM
      - -DCONFIG_SPIRAM=1
      - -DCONFIG_SPIRAM_SPEED_200M=1
      - -DCONFIG_SPIRAM_USE_CAPS_ALLOC=1

      # Allouer LVGL sur PSRAM
      - -DLV_MEM_CUSTOM_ALLOC=heap_caps_malloc
      - -DLV_MEM_CUSTOM_FREE=heap_caps_free
      - '-DLV_MEM_CUSTOM_ALLOC_PARAM=MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT'

esp32:
  board: esp32-p4-function-ev-board
  variant: esp32p4
  framework:
    type: esp-idf
    sdkconfig_options:
      # PSRAM Configuration
      CONFIG_SPIRAM: y
      CONFIG_SPIRAM_SPEED_200M: y
      CONFIG_SPIRAM_USE_CAPS_ALLOC: y
      CONFIG_SPIRAM_IGNORE_NOTFOUND: n

      # Cache Configuration
      CONFIG_CACHE_L2_CACHE_256KB: y
      CONFIG_CACHE_L2_CACHE_LINE_128B: y
```

---

## 🧠 Étape 3 : Configurer ESP-DL pour Utiliser PSRAM

### Modifier les Composants de Détection

#### A. face_detection/__init__.py

Ajoutez l'allocation PSRAM après les build flags :

```python
# Dans async def to_code(config):
# ...après les autres build flags...

# Allocation PSRAM pour modèles ESP-DL
cg.add_build_flag("-DCONFIG_SPIRAM=1")
cg.add_build_flag("-DCONFIG_SPIRAM_USE_MALLOC=1")

# Forcer les modèles dans PSRAM
cg.add_build_flag("-DCONFIG_HUMAN_FACE_DETECT_MODEL_IN_PSRAM=1")
cg.add_build_flag("-DCONFIG_HUMAN_FACE_FEAT_MODEL_IN_PSRAM=1")
```

#### B. yolo11_detection/__init__.py

```python
# Dans async def to_code(config):

cg.add_build_flag("-DCONFIG_SPIRAM=1")
cg.add_build_flag("-DCONFIG_SPIRAM_USE_MALLOC=1")
cg.add_build_flag("-DCONFIG_YOLO11_DETECT_MODEL_IN_PSRAM=1")
```

#### C. pedestrian_detection/__init__.py

```python
# Dans async def to_code(config):

cg.add_build_flag("-DCONFIG_SPIRAM=1")
cg.add_build_flag("-DCONFIG_SPIRAM_USE_MALLOC=1")
cg.add_build_flag("-DCONFIG_PEDESTRIAN_DETECT_MODEL_IN_PSRAM=1")
```

---

## 📊 Étape 4 : Allocation Mémoire Optimisée

### Configuration Multi-Détection avec PSRAM

```yaml
# Avec PSRAM, vous pouvez maintenant utiliser plusieurs détections !

face_detection:
  id: face_detector
  camera_id: tab5_cam
  detection_interval: 12        # Peut être plus fréquent avec PSRAM
  recognition_enabled: true
  draw_enabled: true

yolo11_detection:
  id: yolo11_detector
  camera_id: tab5_cam
  detection_interval: 15
  score_threshold: 0.4
  draw_enabled: true

# Optionnel : Pedestrian si assez de PSRAM
# pedestrian_detection:
#   id: pedestrian_detector
#   camera_id: tab5_cam
#   detection_interval: 10
#   draw_enabled: true
```

### Répartition Mémoire avec PSRAM (16 Mo + 16 Mo)

| Composant | RAM Interne | PSRAM | Total |
|-----------|-------------|-------|-------|
| Système ESP-IDF | 2 Mo | - | 2 Mo |
| LVGL + Framebuffers | 1 Mo | 3 Mo | 4 Mo |
| Face Detection | - | 5 Mo | 5 Mo |
| YOLO11 Detection | - | 7 Mo | 7 Mo |
| Pedestrian (optionnel) | - | 5 Mo | 5 Mo |
| Application | 2 Mo | 1 Mo | 3 Mo |
| **Disponible** | **11 Mo** | **0 Mo** | **11 Mo** ✅

---

## 🚀 Étape 5 : Configuration Complète Waveshare-Style

### Fichier de configuration complet

```yaml
esphome:
  name: esp32-p4-multi-detection
  platformio_options:
    board_build.arduino.memory_type: opi_opi
    build_flags:
      # PSRAM
      - -DBOARD_HAS_PSRAM
      - -DCONFIG_SPIRAM=1
      - -DCONFIG_SPIRAM_SPEED_200M=1
      - -DCONFIG_SPIRAM_USE_CAPS_ALLOC=1

      # LVGL sur PSRAM (style Waveshare)
      - -DLV_MEM_CUSTOM=1
      - -DLV_MEM_SIZE=2097152  # 2 MB pour LVGL
      - '-DLV_MEM_CUSTOM_ALLOC(x)=heap_caps_malloc(x, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)'
      - '-DLV_MEM_CUSTOM_FREE(x)=heap_caps_free(x)'

esp32:
  board: esp32-p4-function-ev-board
  variant: esp32p4
  framework:
    type: esp-idf
    sdkconfig_options:
      CONFIG_SPIRAM: y
      CONFIG_SPIRAM_SPEED_200M: y
      CONFIG_SPIRAM_USE_CAPS_ALLOC: y
      CONFIG_SPIRAM_USE_MALLOC: y
      CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL: 0
      CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL: 32768

      # Cache optimisé
      CONFIG_CACHE_L2_CACHE_256KB: y
      CONFIG_CACHE_L2_CACHE_LINE_128B: y

# Caméra
mipi_dsi_cam:
  id: tab5_cam
  sensor_type: ov02c10
  resolution: "800x480"
  pixel_format: RGB565
  framerate: 30

# LVGL Display
lvgl_camera_display:
  id: camera_display
  camera_id: tab5_cam
  canvas_id: camera_canvas
  update_interval: 100ms

# Multi-Détection (maintenant possible avec PSRAM !)
face_detection:
  id: face_detector
  camera_id: tab5_cam
  detection_interval: 12
  recognition_enabled: true

yolo11_detection:
  id: yolo11_detector
  camera_id: tab5_cam
  detection_interval: 15
  score_threshold: 0.45
  draw_enabled: true

# Monitoring mémoire
interval:
  - interval: 30s
    then:
      - lambda: |-
          size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
          size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
          ESP_LOGI("memory", "RAM: %.2f MB | PSRAM: %.2f MB",
                   free_internal / (1024.0 * 1024.0),
                   free_spiram / (1024.0 * 1024.0));
```

---

## ⚠️ Points Importants

### 1. Vérifier le Matériel

**Votre ESP32-P4 doit avoir PSRAM physiquement présente !**

Modèles avec PSRAM :
- ✅ ESP32-P4-Function-EV-Board (Espressif) - 16 Mo PSRAM
- ✅ Waveshare ESP32-P4 7" Touch LCD - 16 Mo PSRAM
- ❌ Certains modules ESP32-P4 sans PSRAM

### 2. Performance PSRAM

- PSRAM est plus lente que RAM interne (~200 MHz vs 400 MHz)
- Parfait pour stockage modèles (lecture unique au démarrage)
- Bon pour buffers d'images
- Éviter pour code critique temps-réel

### 3. Changements de Code Minimaux

Les modèles ESP-DL chargés en PSRAM fonctionnent de manière transparente :
- Pas de modification du code C++ nécessaire
- Juste des flags de compilation
- ESP-IDF gère l'allocation automatiquement

---

## 🔧 Dépannage

### PSRAM Non Détectée

```
E (123) psram: PSRAM not found
```

**Solutions** :
1. Vérifier que votre board a PSRAM physiquement
2. Essayer `CONFIG_SPIRAM_IGNORE_NOTFOUND: n` pour forcer l'erreur
3. Vérifier le schéma PCB de votre board

### Erreurs d'Allocation

```
E (456) heap: Failed to allocate from SPIRAM
```

**Solutions** :
1. Réduire `LV_MEM_SIZE`
2. Augmenter `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL`
3. Utiliser moins de détections simultanément

### Crash au Boot

```
Guru Meditation Error: Core 0 panic'ed (LoadProhibited)
```

**Solutions** :
1. Vérifier que tous les modèles sont bien configurés pour PSRAM
2. S'assurer que LVGL utilise PSRAM
3. Réduire la mémoire allouée

---

## 📈 Performances Attendues

### Avec PSRAM (16 Mo + 16 Mo = 32 Mo)

| Configuration | Mémoire Utilisée | Status |
|---------------|------------------|--------|
| Face + YOLO11 | ~12 Mo | ✅ Fonctionne |
| Face + Pedestrian | ~10 Mo | ✅ Fonctionne |
| YOLO11 + Pedestrian | ~12 Mo | ✅ Fonctionne |
| Les 3 détections | ~17 Mo | ⚠️ Limite (surveiller) |

### Sans PSRAM (16 Mo uniquement)

| Configuration | Mémoire Utilisée | Status |
|---------------|------------------|--------|
| Face seul | ~5 Mo | ✅ Fonctionne |
| YOLO11 seul | ~7 Mo | ✅ Fonctionne |
| Pedestrian seul | ~5 Mo | ✅ Fonctionne |
| 2 détections | >11 Mo | ❌ Dépassement |

---

## ✅ Checklist de Déploiement

- [ ] Vérifier PSRAM matérielle présente
- [ ] Ajouter configuration PSRAM ESPHome
- [ ] Modifier `__init__.py` des composants détection
- [ ] Ajouter monitoring mémoire
- [ ] Tester avec 1 détection d'abord
- [ ] Ajouter progressivement 2ème et 3ème détection
- [ ] Surveiller logs mémoire toutes les 30s
- [ ] Optimiser `detection_interval` si nécessaire

---

## 🎉 Résultat Final

**Avec PSRAM activée correctement, vous POUVEZ utiliser :**

✅ Face Detection + YOLO11 ensemble
✅ Face Detection + Pedestrian ensemble
✅ YOLO11 + Pedestrian ensemble
⚠️ Les 3 ensemble (surveiller la mémoire)

**Sans PSRAM :**

✅ Une seule détection à la fois (choix entre Face/YOLO11/Pedestrian)

---

## 📚 Références

- [ESP32-P4 Waveshare Examples](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B)
- [ESP-IDF SPIRAM Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/external-ram.html)
- [LVGL Memory Management](https://docs.lvgl.io/master/porting/project.html#configure)
