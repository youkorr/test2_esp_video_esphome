# Analyse du Problème IPA pour OV02C10

## ❌ Problème Identifié

Le capteur OV02C10 possède un fichier de configuration IPA JSON complet (`ov02c10_default.json`) qui contient tous les paramètres optimisés pour ce capteur, **MAIS ces paramètres ne sont jamais utilisés par le pipeline IPA**.

## 🔍 Analyse Technique

### 1. Configuration JSON Embarquée (Non Utilisée ❌)

Le fichier `components/esp_video/src/embedded_ov02c10_ipa_config_json.c` contient :

```c
const char ov02c10_ipa_config_json_start[] = "{ ... }";
const char *ov02c10_ipa_config_json_end = ...;
const size_t ov02c10_ipa_config_json_size = ...;
```

**Contenu du JSON** (11 205 octets) :
- AWB (Auto White Balance) : paramètres de plage RG/BG, gains min/max
- AGC (Auto Gain Control) : contrôle automatique du gain ❌ **NON SUPPORTÉ**
- AE (Auto Exposure) : exposition automatique avec anti-flicker ❌ **NON SUPPORTÉ**
- ACC (Auto Color Correction) : matrice CCM optimisée pour OV02C10
- ADN (Auto Denoising) : réduction de bruit (bilateral filter, demosaic)
- AEN (Auto Enhancement) : gamma, sharpen, contrast

### 2. API ESP-IPA (Limitée)

**Fonction de création** : `esp_ipa_pipeline_create(ipa_nums, ipa_names, handle)`

```c
// Dans components/esp_ipa/include/esp_ipa.h:51
esp_err_t esp_ipa_pipeline_create(
    uint8_t ipa_nums,           // Nombre d'algorithmes
    const char **ipa_names,     // Noms des algorithmes
    esp_ipa_pipeline_handle_t *handle
);
```

❌ **PROBLÈME** : Cette API ne prend PAS de configuration JSON !

### 3. Configuration Actuelle (Générique)

**Fichier** : `components/esp_ipa/src/version.c:64-111`

```c
const esp_ipa_config_t *esp_ipa_pipeline_get_config(const char *cam_name)
{
    // Configuration complète pour OV02C10 et autres
    static const char *ipa_names_full[] = {
        "awb.gray",                /* Auto White Balance */
        "denoising.gain_feedback", /* Réduction bruit */
        "sharpen.freq_feedback",   /* Netteté */
        "gamma.lumma_feedback",    /* Correction gamma */
        "cc.linear",               /* Color Correction Matrix */
    };

    static const esp_ipa_config_t ipa_config_full = {
        .ipa_nums = 5,
        .ipa_names = ipa_names_full,
    };

    // OV02C10 reçoit cette config GÉNÉRIQUE (pas les paramètres JSON optimisés!)
    ESP_LOGI(TAG, "📸 IPA config for %s: AWB+Denoise+Sharpen+Gamma+CCM", cam_name);
    return &ipa_config_full;
}
```

**Résultat** : Les 5 algorithmes sont chargés avec des **paramètres par défaut codés en dur dans `libesp_ipa.a`**, pas les paramètres optimisés du JSON.

### 4. Algorithmes Disponibles vs Non Disponibles

**✅ Algorithmes disponibles dans `libesp_ipa.a`** :
- `awb.gray` : Auto White Balance (balance des blancs)
- `denoising.gain_feedback` : Réduction du bruit
- `sharpen.freq_feedback` : Netteté
- `gamma.lumma_feedback` : Correction gamma
- `cc.linear` : Matrice de correction couleur (CCM)

**❌ Algorithmes NON disponibles** (présents dans le JSON mais absents de libesp_ipa.a) :
- `aec.simple` : Auto Exposure Control (exposition automatique) ❌
- `aec.threshold` : Contrôle d'exposition avec seuils ❌
- `agc.threshold` : Auto Gain Control (gain automatique) ❌

**Source** : `FIX_EXPOSURE_AND_FPS.md:88`
> "AEC/AGC (Auto Exposure Control) n'est PAS disponible dans la version actuelle de libesp_ipa.a"

## 🔴 Conséquences pour OV02C10

### 1. ❌ Pas d'AEC/AGC Automatique
- L'exposition doit être contrôlée **manuellement** via V4L2 (`set_exposure`, `set_gain`)
- Contrairement à d'autres capteurs qui ont AEC/AGC hardware dans leurs registres

### 2. ❌ CCM Non Optimisée
- La matrice CCM utilisée est **générique** (paramètres par défaut de libesp_ipa.a)
- Le JSON contient une CCM calibrée pour OV02C10 :
  ```json
  "ccm": {
    "table": [{
      "color_temp": 0,
      "matrix": [
        1.408,  -0.094, -0.314,
        -0.13,    1.28,  -0.15,
        -0.072,  -0.173,  1.245
      ]
    }]
  }
  ```
- **Résultat** : Couleurs peuvent être incorrectes (teintes décalées)

### 3. ❌ AWB Non Optimisée
- Les plages AWB dans le JSON sont optimisées pour OV02C10 :
  ```json
  "awb": {
    "range": {
      "rg": { "max": 0.9096, "min": 0.573 },
      "bg": { "max": 0.9634, "min": 0.5368 }
    }
  }
  ```
- **Résultat** : Balance des blancs peut mal converger ou donner des teintes incorrectes

### 4. ❌ Paramètres de Denoising/Sharpen/Gamma Génériques
- Le JSON contient des paramètres adaptés au bruit et aux caractéristiques de l'OV02C10
- Les paramètres génériques peuvent ne pas être optimaux

## ✅ Solutions Possibles

### Solution 1 : Contrôle Manuel (Actuel)

**Avantages** :
- Fonctionne immédiatement
- Contrôle précis de l'exposition et du gain

**Inconvénients** :
- Pas d'adaptation automatique aux changements de lumière
- Nécessite calibration manuelle

**Méthodes disponibles** :
```cpp
auto cam = id(mipi_cam);
cam->set_exposure(800);     // Contrôle manuel exposition
cam->set_gain(64);          // Contrôle manuel gain
cam->set_white_balance(6500); // Balance des blancs manuelle (K)
```

**Documentation** : `FIX_EXPOSURE_AND_FPS.md`

### Solution 2 : Migrer vers OV5647 (Recommandé)

**Avantages** :
- AEC/AGC hardware dans les registres du capteur
- Configuration IPA JSON fonctionnelle
- Qualité d'image supérieure

**Inconvénients** :
- Nécessite changement de capteur hardware

**Documentation** : `FIX_EXPOSURE_AND_FPS.md:584-657`

### Solution 3 : Implémenter un Parser JSON Custom (Complexe)

**Approche** :
1. Créer une fonction `esp_ipa_pipeline_create_from_json(json_path, handle)`
2. Parser le JSON manuellement avec cJSON
3. Appliquer les paramètres via l'API esp_ipa existante

**Problème** :
- L'API esp_ipa ne fournit **AUCUNE** fonction pour configurer les paramètres des algorithmes après création
- Il faudrait modifier `libesp_ipa.a` (bibliothèque précompilée) → **IMPOSSIBLE sans source**

**Verdict** : ❌ Non réalisable sans accès au code source de libesp_ipa.a

### Solution 4 : Attendre une Mise à Jour Espressif

**Espoir** :
- Espressif pourrait publier une version de libesp_ipa.a qui :
  - Supporte AEC/AGC
  - Permet de charger des configurations JSON
  - Expose une API pour configurer les paramètres IPA

**Réalisme** : Incertain, aucune roadmap publique disponible

## 📊 Comparaison des Capteurs

| Caractéristique | OV02C10 | OV5647 | SC202CS |
|-----------------|---------|--------|---------|
| **JSON IPA embarqué** | ✅ Oui (11KB) | ✅ Oui (4KB) | ❌ Non |
| **JSON IPA utilisé** | ❌ Non | ❌ Non | ❌ Non |
| **AEC/AGC IPA** | ❌ Non (pas dans libesp_ipa.a) | ❌ Non | ❌ Non |
| **AEC/AGC Hardware** | ❓ Inconnu | ✅ Oui (registres capteur) | ❌ Non |
| **CCM Optimisée** | ❌ Non (générique) | ❌ Non (générique) | ❌ Non |
| **Qualité d'image** | 🟡 Moyenne (sans JSON) | 🟢 Bonne (AEC hardware) | 🔴 Mauvaise |
| **Contrôle exposition** | Manuel (V4L2) | Auto (hardware) | Manuel (V4L2) |

## 🎯 Recommandation Finale

**Pour OV02C10** :
1. ✅ **Court terme** : Utiliser contrôle manuel (`set_exposure`, `set_gain`, `set_white_balance`)
2. 🟡 **Moyen terme** : Tester si AEC/AGC hardware est disponible dans les registres OV02C10
3. 🟢 **Long terme** : Migrer vers OV5647 pour AEC/AGC hardware automatique

**Pourquoi le JSON IPA n'aide pas** :
- libesp_ipa.a ne supporte pas AEC/AGC (algorithmes absents)
- libesp_ipa.a n'a pas d'API pour charger un JSON
- Les paramètres JSON (CCM, AWB ranges, etc.) restent inutilisés

**La seule façon d'utiliser les paramètres JSON serait** :
- Qu'Espressif publie une nouvelle version de libesp_ipa.a avec support JSON
- OU accès au code source de libesp_ipa pour implémenter le parser JSON

## 📝 Fichiers de Référence

- Configuration JSON : `components/esp_cam_sensor/sensor/ov02c10/cfg/ov02c10_default.json`
- JSON embarqué : `components/esp_video/src/embedded_ov02c10_ipa_config_json.c`
- API IPA : `components/esp_ipa/include/esp_ipa.h`
- Version IPA : `components/esp_ipa/src/version.c`
- Pipeline ISP : `components/esp_video/src/esp_video_isp_pipeline.c`
- Documentation : `FIX_EXPOSURE_AND_FPS.md`, `ISP_PIPELINE_ARCHITECTURE.md`
