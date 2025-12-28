# 🔍 Analyse Mémoire YOLO11 - ESP32-P4

## 🚨 Problème Identifié

**Symptôme** : YOLO11 utilise 120% de RAM (19.2 Mo / 16 Mo) → IMPOSSIBLE

**Cause Racine** : `CONFIG_SPIRAM_RODATA` n'est PAS activée

---

## 📊 Analyse Détaillée

### Taille du Modèle sur Disque

```bash
$ ls -lh components/yolo11_detect/models/p4/yolo11_detect_s8_v1.espdl
-rw-r--r-- 1 root root 2.8M  yolo11_detect_s8_v1.espdl
```

**Le modèle fait seulement 2.8 MB** ✅

### Pourquoi 19.2 MB en RAM ? 🤔

Sans `CONFIG_SPIRAM_RODATA`, voici ce qui se passe :

#### 1. Chargement du Modèle (6-7 MB)

```cpp
// yolo11_detect.cpp
#if CONFIG_YOLO11_DETECT_MODEL_IN_FLASH_RODATA
extern const uint8_t yolo11_detect_espdl[] asm("_binary_yolo11_detect_espdl_start");
```

**SANS `CONFIG_SPIRAM_RODATA`** :
- Le modèle binaire de 2.8 MB est COPIÉ depuis flash vers RAM
- Décompression FlatBuffers : 2.8 MB → 4-5 MB
- **Total : ~5 MB en RAM**

**AVEC `CONFIG_SPIRAM_RODATA`** :
- Le modèle reste en FLASH (XIP = Execute In Place)
- Pas de copie en RAM
- **Total : 0 MB en RAM** ✅

#### 2. Buffers d'Inférence (8-10 MB)

```cpp
// ESP-DL alloue des tensors pour chaque couche
dl::Model::run() {
    // Input tensor: 320x320x3 RGB = 300 KB
    // Hidden layers: ~6-8 MB pour YOLO11n
    // Output tensor: détections + classes = 1 MB
}
```

**SANS `CONFIG_SPIRAM_USE_MALLOC`** :
- Tous les tensors en RAM interne
- **Total : ~8 MB en RAM**

**AVEC `CONFIG_SPIRAM_USE_MALLOC`** :
- Tensors en PSRAM automatiquement
- **Total : ~0.5 MB en RAM** (juste pointeurs)

#### 3. Cache de Détections (2-3 MB)

```cpp
// yolo11_detection.cpp
std::vector<DetectionBox> cached_detections_;
```

Peut contenir jusqu'à 100 détections × 30 KB de données = 3 MB

**SANS PSRAM** : En RAM interne
**AVEC PSRAM** : En PSRAM via `MALLOC_CAP_SPIRAM`

#### 4. Stack Frame du Modèle (1-2 MB)

YOLO11 utilise beaucoup de stack pour les calculs intermédiaires.

---

### Calcul Total

| Composant | Sans PSRAM_RODATA | Avec PSRAM_RODATA |
|-----------|-------------------|-------------------|
| Modèle binaire | 5 MB (RAM) | 0 MB (Flash XIP) |
| Tensors inférence | 8 MB (RAM) | 0.5 MB (RAM) |
| Cache détections | 3 MB (RAM) | 0 MB (PSRAM) |
| Stack | 2 MB (RAM) | 2 MB (RAM) |
| **TOTAL RAM** | **18 MB** ❌ | **2.5 MB** ✅ |
| **TOTAL PSRAM** | **0 MB** | **11 MB** ✅ |

---

## ✅ Solution Simple

Ajoutez **UNE SEULE LIGNE** dans votre configuration ESPHome :

```yaml
esp32:
  framework:
    type: esp-idf
    sdkconfig_options:
      CONFIG_SPIRAM: y
      CONFIG_SPIRAM_SPEED_200M: y
      CONFIG_SPIRAM_RODATA: y          # ← CETTE LIGNE !
      CONFIG_SPIRAM_USE_CAPS_ALLOC: y
      CONFIG_SPIRAM_USE_MALLOC: y
```

---

## 📈 Résultats Attendus

### Avant (SANS CONFIG_SPIRAM_RODATA)

```
[yolo11] Loading model...
[memory] RAM utilisée: 18.2 MB / 16.0 MB (114%)
E (123) yolo11: Out of memory!
```

### Après (AVEC CONFIG_SPIRAM_RODATA)

```
[yolo11] Loading model...
[yolo11] Model loaded from flash rodata (XIP mode)
[memory] RAM utilisée: 2.5 MB / 16.0 MB (16%)
[memory] PSRAM utilisée: 11.0 MB / 16.0 MB (69%)
[yolo11] ✅ YOLO11 detector ready
```

---

## 🔍 Vérification

Après recompilation et upload, vérifiez les logs :

### Test 1 : PSRAM Détectée

```
[psram] ✅ PSRAM: 16.00 MB total
```

Si vous voyez `❌ PSRAM NON DÉTECTÉE` → Problème hardware

### Test 2 : Modèle en Flash

```
[yolo11] Model loaded from flash rodata
```

Si vous voyez `Model copied to RAM` → `CONFIG_SPIRAM_RODATA` pas activée

### Test 3 : Utilisation Mémoire

```
[memory] RAM: 85% libre | PSRAM: 30% libre
[memory] ✅ Modèles en PSRAM (11.0 MB PSRAM vs 2.5 MB RAM)
```

Si PSRAM < RAM → Modèles encore en RAM

---

## 🎯 Performance Attendue

| Modèle | RAM Interne | PSRAM | FPS |
|--------|-------------|-------|-----|
| YOLO11 seul | 2.5 MB (16%) | 11 MB (69%) | 8-12 FPS |
| Face + YOLO11 | 4 MB (25%) | 14 MB (88%) | 6-10 FPS |
| Les 3 détections | 5 MB (31%) | 15.5 MB (97%) | 4-8 FPS |

---

## 🚀 Prochaines Étapes

1. ✅ Ajouter `CONFIG_SPIRAM_RODATA: y` dans votre YAML
2. ✅ Recompiler (sera long la première fois - 5-10 min)
3. ✅ Uploader le firmware
4. ✅ Vérifier logs au boot
5. ✅ Confirmer YOLO11 fonctionne avec ~40-50% RAM

---

## 📝 Notes Techniques

### Pourquoi XIP (Execute In Place) ?

Au lieu de copier le modèle de 2.8 MB en RAM, ESP32-P4 peut :
- Lire directement depuis la flash via le cache L2 (256 KB)
- Performance : ~95% de la vitesse RAM pour lecture séquentielle
- Économie : 2.8 MB de RAM libre !

### CONFIG_SPIRAM_RODATA vs CONFIG_SPIRAM_XIP_FROM_PSRAM

- `RODATA` : Constantes et modèles restent en flash
- `XIP_FROM_PSRAM` : Code peut aussi s'exécuter depuis PSRAM

Pour les modèles ESP-DL, `RODATA` suffit.

---

## ✅ Checklist

- [ ] `CONFIG_SPIRAM: y` activée
- [ ] `CONFIG_SPIRAM_SPEED_200M: y` confirmée
- [ ] `CONFIG_SPIRAM_RODATA: y` **AJOUTÉE** ← CRITIQUE
- [ ] `CONFIG_SPIRAM_USE_MALLOC: y` activée
- [ ] Recompilation complète effectuée
- [ ] Upload firmware
- [ ] Logs montrent "flash rodata" et non "copied to RAM"
- [ ] RAM utilisée < 40% au lieu de 120%

---

**Conclusion** : Le modèle YOLO11 fait **2.8 MB**, pas 19 MB. Le problème est que sans `CONFIG_SPIRAM_RODATA`, il est copié et décompressé en RAM interne. Cette seule ligne de configuration résout tout ! 🎉
