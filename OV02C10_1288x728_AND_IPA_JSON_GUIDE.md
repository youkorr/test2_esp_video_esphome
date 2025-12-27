# Guide: OV02C10 Format 1288x728 et Configuration IPA JSON

## ✅ Nouveau Format 1288x728 Disponible

### 📐 Spécifications

**Résolution** : 1288x728 @ 30fps RAW10
- **Aspect ratio** : ~16:9 (1.77:1)
- **Utilisation du capteur** : Full sensor avec ISP downscaling
- **MIPI** : 1 lane @ 400 Mbps (OV02C10_MIPI_CSI_LINE_RATE_800x640_50FPS)
- **XCLK** : 24 MHz
- **Format** : RAW10 (Bayer GBRG)

**Avantages** :
- Format 16:9 optimisé pour les écrans modernes
- Plus haute résolution que 640x368 tout en restant performant
- Compatible avec l'ISP pour traitement d'image
- 30 FPS stable

### 📝 Utilisation dans ESPHome YAML

```yaml
esp_cam_sensor:
  id: tab5_cam
  i2c_id: i2c_bus
  sensor_type: ov02c10
  resolution: "1288x728"    # Nouveau format Near HD 16:9
  pixel_format: "RGB565"
  framerate: 30
  jpeg_quality: 15
  rotation: 270             # Optionnel (0, 90, 180, 270)
  mirror_x: true            # Optionnel
  mirror_y: false           # Optionnel
  crop_offset_x: 0
  crop_offset_y: 0
  ppa_enabled: true

esp_video:
  i2c_id: bsp_bus
  xclk_pin: GPIO36
  xclk_freq: 24000000
  enable_h264: false
  enable_jpeg: true
  enable_isp: true          # Active le pipeline IPA (AWB, CCM, etc.)
  use_heap_allocator: true
```

**Notes importantes** :
- Le format est automatiquement sélectionné par ESPHome en fonction de la `resolution` spécifiée
- `esp_cam_sensor` : Configuration du capteur OV02C10 (résolution, framerate, etc.)
- `esp_video` : Configuration du pipeline vidéo ESP32-P4 (ISP, encodeurs JPEG/H264, etc.)
- `enable_isp: true` active les algorithmes IPA (AWB, CCM, sharpen, denoise, gamma)

### 🔧 Utilisation en C++

```cpp
#include "ov02c10_custom_formats.h"

// Utiliser le format 1288x728
auto& format = ov02c10_format_1288x728_raw10_30fps;

// Configuration du capteur
esp_cam_sensor_format_t config = {
    .name = format.name,
    .format = ESP_CAM_SENSOR_PIXFORMAT_RAW10,
    .width = 1288,
    .height = 728,
    .fps = 30,
    // ... reste de la config
};
```

### 📊 Comparaison avec les Autres Formats

| Format | Résolution | Aspect Ratio | FPS | Usage |
|--------|-----------|--------------|-----|-------|
| **1288x728** | **Near HD** | **16:9** | **30** | **Optimal pour écrans 16:9** |
| 640x368 | VGA Near 16:9 | ~16:9 | 30 | Bas débit, rotation safe |
| 640x480 | VGA | 4:3 | 30 | Standard 4:3 |
| 800x600 | SVGA | 4:3 | 30 | SVGA standard |
| 1920x1080 | Full HD | 16:9 | 30 | Haute résolution |

---

## ❌ Configuration JSON IPA : Problème Technique

### 🔍 Situation Actuelle

Le fichier `/components/esp_cam_sensor/sensor/ov02c10/cfg/ov02c10_default.json` existe et contient une configuration IPA complète avec :

- **AWB** (Auto White Balance) : plages optimisées pour OV02C10
- **AGC** (Auto Gain Control) : paramètres de gain automatique ❌ non supporté
- **AE** (Auto Exposure) : exposition automatique avec anti-flicker ❌ non supporté
- **ACC** (Auto Color Correction) : matrice CCM calibrée
- **ADN** (Auto Denoising) : réduction de bruit optimisée
- **AEN** (Auto Enhancement) : gamma, sharpen, contrast

**Problème** : Ce fichier JSON **n'est pas utilisé** car :

1. ❌ `libesp_ipa.a` (bibliothèque précompilée Espressif) n'a **pas d'API** pour charger un JSON
2. ❌ L'API actuelle ne prend que des noms d'algorithmes, pas de paramètres
3. ❌ Les algorithmes AEC/AGC ne sont **pas disponibles** dans libesp_ipa.a

### 📋 API Actuelle (Limitée)

```c
// API esp_ipa actuelle - NE SUPPORTE PAS le chargement de JSON
esp_err_t esp_ipa_pipeline_create(
    uint8_t ipa_nums,           // Nombre d'algorithmes
    const char **ipa_names,     // Noms des algorithmes
    esp_ipa_pipeline_handle_t *handle
);

// Exemple d'utilisation actuelle
const char *ipa_names[] = {
    "awb.gray",                // AWB avec params PAR DÉFAUT
    "denoising.gain_feedback", // Denoise avec params PAR DÉFAUT
    "sharpen.freq_feedback",   // Sharpen avec params PAR DÉFAUT
    "gamma.lumma_feedback",    // Gamma avec params PAR DÉFAUT
    "cc.linear",               // CCM avec matrice PAR DÉFAUT
};

esp_ipa_config_t config = {
    .ipa_nums = 5,
    .ipa_names = ipa_names,
};

esp_ipa_pipeline_create(5, ipa_names, &handle);
// ↑ Utilise des paramètres PAR DÉFAUT codés en dur dans libesp_ipa.a
// ↑ PAS les paramètres optimisés du JSON!
```

### 🔴 Conséquences

1. **AWB non optimisé** : Utilise des plages génériques, pas celles du JSON
   ```json
   // JSON OV02C10 (IGNORÉ) :
   "awb": {
     "range": {
       "rg": { "max": 0.9096, "min": 0.573 },
       "bg": { "max": 0.9634, "min": 0.5368 }
     }
   }

   // Réalité : Plages AWB génériques de libesp_ipa.a
   ```

2. **CCM non calibrée** : Utilise une matrice générique
   ```json
   // JSON OV02C10 (IGNORÉ) :
   "ccm": {
     "matrix": [
       2.0000,  -0.5459, -0.4541,
       -0.4751,   1.7696, -0.2945,
       -0.2002,  -0.7998,  2.0000
     ]
   }

   // Réalité : CCM générique non calibrée pour OV02C10
   ```

3. **Pas d'AEC/AGC** : Algorithmes non disponibles
   ```json
   // JSON OV02C10 (INUTILISABLE) :
   "agc": { ... }  // ❌ Algorithme n'existe pas dans libesp_ipa.a
   "aec": { ... }  // ❌ Algorithme n'existe pas dans libesp_ipa.a
   ```

---

## 💡 Solutions de Contournement

### Option 1 : Contrôle Manuel (Recommandé)

Utilisez les méthodes de contrôle manuel disponibles :

```cpp
// Accéder au composant caméra
auto cam = id(tab5_cam);

// Contrôle manuel de l'exposition
cam->set_exposure(800);     // Valeur : 100-3000

// Contrôle manuel du gain
cam->set_gain(64);          // Valeur : 16-128

// Balance des blancs manuelle
cam->set_white_balance(6500); // Température Kelvin : 2800-10000

// Réinitialiser AWB auto
cam->reset_white_balance();
```

**Documentation** : Voir `FIX_EXPOSURE_AND_FPS.md` pour guide complet

### Option 2 : Migrer vers OV5647 (Meilleure Qualité)

Le capteur OV5647 offre :
- ✅ AEC/AGC **hardware** dans les registres du capteur
- ✅ Pas besoin de libesp_ipa.a pour l'exposition automatique
- ✅ Meilleure qualité d'image globale

**Documentation** : Voir `FIX_EXPOSURE_AND_FPS.md:584-657`

---

## 🔧 Ce Qu'il Faudrait Faire (Si On Avait Accès au Code Source)

### Approche Théorique

Si on avait accès au code source de `libesp_ipa.a`, voici comment charger le JSON :

```c
#include "cJSON.h"  // Parser JSON

// 1. Charger le fichier JSON
extern const char ov02c10_ipa_config_json_start[];
extern const size_t ov02c10_ipa_config_json_size;

// 2. Parser le JSON
cJSON *root = cJSON_Parse(ov02c10_ipa_config_json_start);
cJSON *ov02c10 = cJSON_GetObjectItem(root, "OV02C10");

// 3. Extraire les paramètres AWB
cJSON *awb = cJSON_GetObjectItem(ov02c10, "awb");
cJSON *range = cJSON_GetObjectItem(awb, "range");
cJSON *rg = cJSON_GetObjectItem(range, "rg");

float rg_min = cJSON_GetObjectItem(rg, "min")->valuedouble;  // 0.573
float rg_max = cJSON_GetObjectItem(rg, "max")->valuedouble;  // 0.9096

// 4. Appliquer les paramètres à l'IPA
// ❌ PROBLÈME : Cette API n'existe pas!
esp_ipa_awb_set_range(ipa_handle, rg_min, rg_max, bg_min, bg_max);

// 5. Extraire CCM
cJSON *ccm = cJSON_GetObjectItem(ov02c10, "acc");
cJSON *table = cJSON_GetObjectItem(ccm, "ccm")->child;
cJSON *matrix = cJSON_GetObjectItem(table, "matrix");

float ccm_matrix[9];
for (int i = 0; i < 9; i++) {
    ccm_matrix[i] = cJSON_GetArrayItem(matrix, i)->valuedouble;
}

// ❌ PROBLÈME : Cette API n'existe pas!
esp_ipa_ccm_set_matrix(ipa_handle, ccm_matrix);

// etc. pour tous les paramètres...
```

**Problème** : Les fonctions `esp_ipa_awb_set_range()`, `esp_ipa_ccm_set_matrix()`, etc. **n'existent pas** dans libesp_ipa.a !

### Ce Qui Manque

Pour charger le JSON IPA, il faudrait qu'Espressif ajoute à `libesp_ipa.a` :

1. ✅ **API de configuration** :
   ```c
   esp_err_t esp_ipa_awb_configure(handle, awb_config_t *config);
   esp_err_t esp_ipa_ccm_configure(handle, ccm_config_t *config);
   esp_err_t esp_ipa_sharpen_configure(handle, sharpen_config_t *config);
   // etc.
   ```

2. ✅ **Fonction de chargement JSON** :
   ```c
   esp_err_t esp_ipa_pipeline_load_json(
       const char *json_data,
       esp_ipa_pipeline_handle_t *handle
   );
   ```

3. ✅ **Algorithmes AEC/AGC** :
   ```c
   // Actuellement NON disponibles dans libesp_ipa.a
   "aec.simple"         // Auto Exposure Control
   "agc.threshold"      // Auto Gain Control
   ```

---

## 📊 Résumé des Limitations IPA

| Fonctionnalité | Disponible | Utilisable | Notes |
|----------------|-----------|-----------|-------|
| **Fichier JSON embarqué** | ✅ Oui | ❌ Non | Fichier existe mais API ne le charge pas |
| **AWB (Balance blancs)** | ✅ Oui | 🟡 Partiel | Fonctionne avec params génériques |
| **CCM (Correction couleur)** | ✅ Oui | 🟡 Partiel | Matrice générique, pas celle du JSON |
| **Denoising** | ✅ Oui | 🟡 Partiel | Params génériques |
| **Sharpen** | ✅ Oui | 🟡 Partiel | Params génériques |
| **Gamma** | ✅ Oui | 🟡 Partiel | Courbe générique |
| **AEC (Auto Exposure)** | ❌ Non | ❌ Non | Pas dans libesp_ipa.a |
| **AGC (Auto Gain)** | ❌ Non | ❌ Non | Pas dans libesp_ipa.a |

**Légende** :
- ✅ Disponible et fonctionne
- 🟡 Disponible mais sous-optimal (params génériques au lieu de ceux du JSON)
- ❌ Non disponible

---

## 🎯 Recommandations

### Pour Obtenir la Meilleure Qualité d'Image avec OV02C10

1. **Utiliser le format 1288x728** (maintenant disponible)
   ```yaml
   esp_cam_sensor:
     resolution: "1288x728"
     pixel_format: "RGB565"
     framerate: 30
   ```

2. **Activer l'ISP** pour bénéficier des algorithmes IPA disponibles
   ```yaml
   esp_video:
     enable_isp: true
   ```

3. **Contrôle manuel de l'exposition et du gain**
   ```cpp
   auto cam = id(tab5_cam);
   cam->set_exposure(800);  // Ajuster selon l'éclairage
   cam->set_gain(64);       // Ajuster selon le bruit
   ```

4. **Surveiller la balance des blancs**
   - Laisser AWB converger (5-10 secondes après démarrage)
   - Si couleurs incorrectes : `cam->set_white_balance(6500);`

### Pour une Solution Automatique Complète

**Migrer vers OV5647** :
- AEC/AGC hardware intégré
- Pas besoin de contrôle manuel
- Meilleure qualité globale

---

## 📄 Fichiers de Référence

- **Format 1288x728** : `components/esp_cam_sensor/sensor/ov02c10/ov02c10.c:1113-1130`
- **Custom formats header** : `components/esp_cam_sensor/ov02c10_custom_formats.h`
- **Registres 1288x728** : `components/esp_cam_sensor/sensor/ov02c10/private_include/ov02c10_settings.h:44-270`
- **JSON IPA (non utilisé)** : `components/esp_cam_sensor/sensor/ov02c10/cfg/ov02c10_default.json`
- **JSON embarqué** : `components/esp_video/src/embedded_ov02c10_ipa_config_json.c`
- **API IPA** : `components/esp_ipa/include/esp_ipa.h`
- **Version IPA** : `components/esp_ipa/src/version.c`
- **Pipeline ISP** : `components/esp_video/src/esp_video_isp_pipeline.c`

---

## 💬 FAQ

### Q: Pourquoi le JSON IPA existe-t-il s'il n'est pas utilisé?

**R:** Le JSON a probablement été créé pour une version future de libesp_ipa.a qui supporterait le chargement de configurations. Pour l'instant, il sert de documentation des paramètres optimaux pour OV02C10.

### Q: Est-ce que je peux modifier libesp_ipa.a pour ajouter le support JSON?

**R:** Non, c'est une bibliothèque précompilée (.a) fournie par Espressif. Sans le code source, il est impossible d'ajouter de nouvelles fonctionnalités.

### Q: Espressif va-t-il ajouter le support JSON à libesp_ipa.a?

**R:** Inconnu. Il n'y a pas de roadmap publique. Le JSON existe déjà, donc c'est possible qu'ils prévoient de l'implémenter à l'avenir.

### Q: Le format 1288x728 est-il aussi performant que 640x368?

**R:** Oui, les deux tournent à 30 FPS. 1288x728 a une résolution ~4x plus élevée, ce qui peut nécessiter plus de bande passante réseau pour le streaming.

### Q: Puis-je utiliser plusieurs formats simultanément?

**R:** Non, un seul format peut être actif à la fois. Vous devez reconfigurer le capteur pour changer de format.
