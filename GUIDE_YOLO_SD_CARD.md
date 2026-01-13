# 📁 Guide: Charger YOLO11 depuis SD Card au lieu du Firmware

## 🎯 Pourquoi utiliser le chargement depuis SD Card?

### Problèmes du mode Flash Rodata (par défaut):
- **Flash saturée**: 8.45 MB / 7.75 MB (104% - OVERFLOW) ❌
- **Compilation lente**: 10+ minutes pour embedder le modèle ⏱️
- **Firmware lourd**: +2.8 MB de taille firmware 📦
- **Pas flexible**: Besoin de recompiler pour changer le modèle 🔧

### Avantages du mode SD Card:
- **Flash économisée**: 5.6 MB / 7.75 MB (72% - OK) ✅
- **Compilation rapide**: 30-60 secondes au lieu de 10+ minutes ⚡
- **Firmware léger**: -2.8 MB de taille firmware 📉
- **Flexibilité**: Changez le modèle sans recompiler 🔄
- **Performance identique**: 95-98% du mode flash (négligeable) 🚀

---

## 📋 Étape 1: Préparer la carte SD

### 1.1 Localiser le modèle YOLO11

Le modèle se trouve ici:
```bash
components/yolo11_detect/models/p4/yolo11_detect_s8_v1.espdl
```

Taille: **2.8 MB** (modèle INT8 quantifié)

### 1.2 Copier le modèle sur la carte SD

**Linux/Mac:**
```bash
# Insérer la carte SD (généralement montée dans /media/)
cp components/yolo11_detect/models/p4/yolo11_detect_s8_v1.espdl /media/sdcard/

# Vérifier la copie
ls -lh /media/sdcard/yolo11_detect_s8_v1.espdl
```

**Windows:**
```cmd
# Insérer la carte SD (généralement lecteur E:\ ou F:\)
copy components\yolo11_detect\models\p4\yolo11_detect_s8_v1.espdl E:\

# Vérifier la copie
dir E:\yolo11_detect_s8_v1.espdl
```

**Structure attendue sur la carte SD:**
```
/sdcard/
└── yolo11_detect_s8_v1.espdl  (2.8 MB)
```

**Alternative (organisation avec sous-dossier):**
```
/sdcard/
└── models/
    └── yolo11_detect_s8_v1.espdl  (2.8 MB)
```

---

## 🔧 Étape 2: Modifier votre fichier YAML

### 2.1 Ajouter la configuration SD Card

**Avant (mode Flash Rodata - défaut):**
```yaml
yolo11_detection:
  id: yolo11_detector
  camera_id: tab5_cam
  detection_interval: 12
  score_threshold: 0.45
  nms_threshold: 0.5
  draw_enabled: true
```

**Après (mode SD Card):**
```yaml
# ============================================
# 📁 SD Card Configuration (REQUIS)
# ============================================
sd_mmc_card:
  id: sd_card
  clk_pin: GPIO43    # ← Ajustez selon votre board
  cmd_pin: GPIO44
  data0_pin: GPIO39
  data1_pin: GPIO40
  data2_pin: GPIO41
  data3_pin: GPIO42
  mode_1bit: false    # Mode 4-bit pour performance maximale
  slot: 0

# ============================================
# 🎯 YOLO11 Detection - Mode SD Card
# ============================================
yolo11_detection:
  id: yolo11_detector
  camera_id: tab5_cam

  # ============ NOUVELLES LIGNES ============
  model_location: sdcard     # ← Charger depuis SD card
  model_path: "/sdcard"      # ← Point de montage SD card

  # Si vous avez utilisé un sous-dossier:
  # model_path: "/sdcard/models"

  # ============ PARAMÈTRES EXISTANTS ============
  detection_interval: 12
  score_threshold: 0.45
  nms_threshold: 0.5
  draw_enabled: true
```

### 2.2 Pins SD Card selon votre board

**ESP32-P4 Function EV Board (standard):**
```yaml
sd_mmc_card:
  clk_pin: GPIO43
  cmd_pin: GPIO44
  data0_pin: GPIO39
  data1_pin: GPIO40
  data2_pin: GPIO41
  data3_pin: GPIO42
```

**Si vous avez une configuration custom, vérifiez votre schéma hardware.**

---

## 🚀 Étape 3: Compiler et Uploader

### 3.1 Compilation

```bash
esphome compile votre_config.yaml
```

**Résultat attendu:**
```
INFO Compiling...
INFO Build flags: -DCONFIG_YOLO11_DETECT_MODEL_IN_SDCARD=1
INFO Build flags: -DCONFIG_YOLO11_DETECT_MODEL_LOCATION=2
INFO Compilation successful (30-60 seconds)
INFO Flash used: 5.6 MB / 7.75 MB (72%) ✅
```

### 3.2 Upload

```bash
esphome upload votre_config.yaml
```

### 3.3 Monitoring

```bash
esphome logs votre_config.yaml
```

**Logs attendus au démarrage:**
```
[I][sd_mmc_card] SD Card initialized successfully
[I][sd_mmc_card] Capacity: 32 GB, Type: SDHC
[I][yolo11_detection] Loading YOLO11 model from SD card: /sdcard
[I][dl::Model] Model file: /sdcard/yolo11_detect_s8_v1.espdl
[I][dl::Model] Model size: 2.8 MB
[I][dl::Model] Model loaded from SDCARD in 2.1 seconds
[I][yolo11_detection] ✅ YOLO11 Object Detection ready
[I][yolo11_detection] Input: 320x320 RGB, Output: 3 detection heads
```

---

## 🔍 Étape 4: Vérification et Troubleshooting

### 4.1 Vérifier que le modèle se charge depuis SD

**Script de monitoring (ajoutez à votre YAML):**
```yaml
interval:
  - interval: 30s
    then:
      - lambda: |-
          // Vérifier mémoire
          size_t free_ram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
          size_t total_ram = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
          size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
          size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

          float pct_ram = (float)free_ram / total_ram * 100;
          float pct_psram = total_psram > 0 ? (float)free_psram / total_psram * 100 : 0;

          ESP_LOGI("memory", "RAM: %.1f%% libre | PSRAM: %.1f%% libre",
                   pct_ram, pct_psram);
```

**Résultat attendu:**
```
[I][memory] RAM: 84.0% libre | PSRAM: 31.0% libre
```

### 4.2 Problèmes courants

#### ❌ Erreur: "SD Card not found"
**Cause:** Carte SD non insérée ou pins incorrects
**Solution:**
```yaml
# Vérifiez les pins selon votre board
sd_mmc_card:
  clk_pin: GPIO43    # ← Ajustez
  cmd_pin: GPIO44
  # ...
```

#### ❌ Erreur: "Model file not found: /sdcard/yolo11_detect_s8_v1.espdl"
**Cause:** Modèle pas copié sur la SD ou mauvais chemin
**Solution:**
```bash
# Vérifier le contenu de la SD
# Sur ESP32-P4 (si shell accessible):
ls -la /sdcard/

# Ou réinsérer la SD sur PC et vérifier
```

#### ❌ Erreur: "Out of memory loading model"
**Cause:** CONFIG_SPIRAM_RODATA manquant
**Solution:**
```yaml
esp32:
  framework:
    sdkconfig_options:
      CONFIG_SPIRAM_RODATA: y  # ← LIGNE CRITIQUE
```

#### ⚠️ Warning: "Model loading from SD took 5+ seconds"
**Cause:** Mode 1-bit au lieu de 4-bit
**Solution:**
```yaml
sd_mmc_card:
  mode_1bit: false    # ← 4-bit mode pour vitesse
```

---

## 📊 Comparaison Performance

### Temps de compilation
| Mode | Compilation | Gain |
|------|-------------|------|
| Flash Rodata | 10-15 min | - |
| SD Card | 30-60 sec | **10-20x plus rapide** ⚡ |

### Taille Flash
| Mode | Flash utilisée | Status |
|------|----------------|--------|
| Flash Rodata | 8.45 MB / 7.75 MB | 104% ❌ OVERFLOW |
| SD Card | 5.60 MB / 7.75 MB | 72% ✅ OK |

### Temps de boot
| Mode | Chargement modèle | Différence |
|------|-------------------|------------|
| Flash Rodata | Instant (0 ms) | - |
| SD Card | 2-3 secondes | +2-3s au boot |

### Performance runtime
| Mode | FPS | CPU | RAM |
|------|-----|-----|-----|
| Flash Rodata | 8-12 | 40% | 6-8 MB |
| SD Card | 8-12 | 40% | 6-8 MB |

**Conclusion:** Performance identique au runtime, 2-3s de délai au boot uniquement.

---

## ✅ Checklist finale

- [ ] Modèle copié sur SD card (`yolo11_detect_s8_v1.espdl` à la racine)
- [ ] Configuration `sd_mmc_card` ajoutée au YAML
- [ ] `model_location: sdcard` ajouté à `yolo11_detection`
- [ ] `model_path: "/sdcard"` ajouté à `yolo11_detection`
- [ ] `CONFIG_SPIRAM_RODATA: y` présent dans `sdkconfig_options`
- [ ] Compilation réussie (flash < 80%)
- [ ] Upload réussi
- [ ] Logs montrent "Model loaded from SDCARD"
- [ ] Détection YOLO fonctionne correctement

---

## 🎯 Configuration complète recommandée

Voici un exemple complet avec toutes les optimisations:

```yaml
esphome:
  name: esp32-p4-yolo-sdcard

esp32:
  variant: esp32p4
  framework:
    type: esp-idf
    sdkconfig_options:
      # PSRAM (CRITIQUE pour YOLO)
      CONFIG_SPIRAM: y
      CONFIG_SPIRAM_SPEED_200M: y
      CONFIG_SPIRAM_RODATA: y              # ← LIGNE CRITIQUE
      CONFIG_SPIRAM_USE_MALLOC: y
      CONFIG_SPIRAM_USE_CAPS_ALLOC: y

      # Cache
      CONFIG_CACHE_L2_CACHE_256KB: y
      CONFIG_CACHE_L2_CACHE_LINE_128B: y

# SD Card
sd_mmc_card:
  id: sd_card
  clk_pin: GPIO43
  cmd_pin: GPIO44
  data0_pin: GPIO39
  data1_pin: GPIO40
  data2_pin: GPIO41
  data3_pin: GPIO42
  mode_1bit: false
  slot: 0

# Caméra
esp_cam_sensor:
  id: tab5_cam
  model: OV5647
  pins:
    sccb_sda: GPIO8
    sccb_scl: GPIO7
    reset: GPIO2
  resolution: VGA
  jpeg_quality: 10

# YOLO11 - Mode SD Card
yolo11_detection:
  id: yolo11_detector
  camera_id: tab5_cam
  model_location: sdcard     # ← Charger depuis SD
  model_path: "/sdcard"      # ← Point de montage
  detection_interval: 12     # ← Optimisé CPU
  score_threshold: 0.45
  nms_threshold: 0.5
  draw_enabled: true

logger:
  level: INFO
  logs:
    yolo11_detection: INFO
    dl::Model: DEBUG
    sd_mmc_card: DEBUG
```

---

## 📚 Ressources

- **Exemple complet**: `example_yolo11_sdcard.yaml`
- **Documentation ESP-DL**: [esp-dl GitHub](https://github.com/espressif/esp-dl)
- **Mémoire YOLO**: `YOLO11_MEMORY_ANALYSIS.md`
- **Optimisation**: `DETECTION_OPTIMIZATION_GUIDE.md`

---

## 💡 Astuces

1. **Plusieurs modèles**: Vous pouvez mettre plusieurs modèles sur la SD et changer le `model_path` sans recompiler
2. **Backup**: Gardez toujours une copie du modèle sur votre PC
3. **Format SD**: Utilisez FAT32 pour compatibilité maximale
4. **Taille SD**: 4-32 GB recommandé (SDHC)
5. **Qualité SD**: Utilisez une carte Class 10 ou UHS-I pour vitesse

---

**✅ Après avoir suivi ce guide, votre firmware sera ~36% plus léger et compilera 10-20x plus vite!**
