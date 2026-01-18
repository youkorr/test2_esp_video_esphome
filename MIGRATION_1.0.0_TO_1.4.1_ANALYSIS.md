# Analyse: Pourquoi l'ISP/IPA fonctionnait avant et plus maintenant

## 🔍 Découverte importante

Vous aviez **raison** de dire que "avant la mise à jour d'esp-video je n'avais pas de problème et tout fonctionnait très bien"!

Après analyse de l'historique git, j'ai découvert que vous aviez **une solution fonctionnelle** pour ISP/IPA avec esp-video 1.0.0, mais elle a été **complètement supprimée** lors de la mise à jour vers 1.4.1.

## ✅ Situation AVANT la mise à jour (esp-video 1.0.0)

### Fichiers qui existaient et **FONCTIONNAIENT**:

1. **components/esp_ipa/src/esp_ipa_json_loader.c** (526 lignes)
   - Parsait les JSON embarqués (ov5647_default.json, ov02c10_default.json) avec cJSON
   - Extrayait les paramètres calibrés:
     - CCM (Color Correction Matrix) → corrigeait les couleurs fades
     - AWB ranges (Auto White Balance) → balance des blancs
     - Sharpen params → netteté
     - Gamma → contraste/luminosité
     - Contrast → contraste
   - **Appliquait DIRECTEMENT à l'ISP matériel via V4L2 ioctl**

2. **components/esp_ipa/src/esp_ipa_detect_stubs.c** (56 lignes)
   - Déclarait les fonctions IPA de libesp_ipa.a:
     - `__esp_ipa_detect_fn_awb_gray_world`
     - `__esp_ipa_detect_fn_agc_threshold`
     - `__esp_ipa_detect_fn_denoising_gain_feedback`
     - `__esp_ipa_detect_fn_sharpen_freq_feedback`
     - `__esp_ipa_detect_fn_gamma_lumma_feedback`
     - `__esp_ipa_detect_fn_cc_linear`
   - Créait un tableau fonctionnel pour l'itération

3. **components/esp_ipa/include/esp_ipa_json_loader.h** (127 lignes)
   - Déclarations pour esp_ipa_load_json_config()
   - Structures pour stocker la config parsée

### Comment ça fonctionnait:

```
1. esp_ipa_json_loader.c parsait le JSON avec cJSON
2. Extrayait les matrices CCM calibrées pour le sensor
3. Extrayait les ranges AWB optimisés
4. Extrayait sharpen, gamma, contrast
5. Appliquait TOUT via V4L2 ioctl directement à l'ISP matériel
6. ✅ Balance des blancs: OK
7. ✅ Couleurs correctes: OK (grâce à CCM)
8. ✅ Netteté: OK
9. ✅ Exposition: OK
```

**Résultat**: Images **parfaites** avec OV5647, SC202CS, OV02C10!

## ❌ Situation APRÈS la mise à jour (esp-video 1.4.1)

### Ce qui s'est passé (commit 261ccb9):

```
components/esp_ipa/src/esp_ipa_json_loader.c    | 526 lignes SUPPRIMÉES ❌
components/esp_ipa/src/esp_ipa_detect_stubs.c   |  56 lignes SUPPRIMÉES ❌
components/esp_ipa/include/esp_ipa_json_loader.h | 127 lignes SUPPRIMÉES ❌
```

**TOUT le code fonctionnel a été supprimé!**

### Pourquoi?

La mise à jour vers esp-video 1.4.1 a apporté une nouvelle version de libesp_ipa.a qui:
- Change complètement l'API
- Ne supporte plus l'approche directe via V4L2
- Nécessite de passer par `esp_ipa_pipeline_create()` avec des structures complexes

### Ce que j'ai fait (par erreur):

J'ai créé de NOUVEAUX fichiers:
- `esp_ipa_json_loader.c`: Retourne NULL → désactive ISP/IPA
- `esp_ipa_detect_stubs.c`: Tableau vide → pas d'IPAs

**Résultat**: ISP/IPA complètement désactivé ❌

## 🛠️ Solution: Restaurer l'ancien code

### Option 1: Restaurer l'approche V4L2 directe ✅ RECOMMANDÉ

**Avantages**:
- Code déjà écrit et testé
- Fonctionnait parfaitement avant
- Applique directement CCM, AWB, sharpen, gamma
- Simple et efficace

**Inconvénients**:
- Utilise V4L2 directement (bypass le pipeline IPA)
- Peut ne pas être compatible avec ISP nouveau dans 1.4.1

**Effort**: 1-2 heures (restaurer l'ancien code)

### Option 2: Adapter au nouveau pipeline IPA

**Avantages**:
- Utilise l'API officielle esp_ipa_pipeline_create()
- Compatible long terme

**Inconvénients**:
- Très complexe (50+ structures à remplir)
- Nécessite mapper JSON → esp_ipa_config_t
- 2-3 jours de développement

## 📋 Ancien code esp_ipa_json_loader.c (résumé)

### Fonction principale: `esp_ipa_load_json_config()`

```c
esp_err_t esp_ipa_load_json_config(const char *sensor_name, esp_ipa_json_config_t *ipa_json_config)
{
    // 1. Sélectionner JSON embarqué (OV5647 ou OV02C10)
    const char *json_data = ov5647_ipa_config_json_start; // ou ov02c10

    // 2. Parser avec cJSON
    cJSON *root = cJSON_Parse(json_data);
    cJSON *sensor_root = cJSON_GetObjectItem(root, "OV5647");

    // 3. Extraire CCM (critique pour couleurs)
    parse_ccm_from_json(sensor_root, &ipa_json_config->ccm);

    // 4. Extraire AWB ranges (critique pour balance blancs)
    parse_awb_from_json(sensor_root, &ipa_json_config->awb);

    // 5. Extraire sharpen, gamma, contrast
    parse_sharpen_from_json(sensor_root, &ipa_json_config->sharpen);
    parse_gamma_from_json(sensor_root, &ipa_json_config->gamma);
    parse_contrast_from_json(sensor_root, &ipa_json_config->contrast);

    return ESP_OK;
}
```

### Fonction d'application: `esp_ipa_apply_json_to_isp()`

```c
esp_err_t esp_ipa_apply_json_to_isp(int isp_fd, const esp_ipa_json_config_t *ipa_json_config)
{
    struct v4l2_ext_controls controls;
    struct v4l2_ext_control control[1];

    // 1. Appliquer CCM via V4L2_CID_USER_ESP_ISP_CCM
    esp_video_isp_ccm_t ccm = { .enable = true };
    memcpy(ccm.matrix, ipa_json_config->ccm.matrix, sizeof(ccm.matrix));
    control[0].id = V4L2_CID_USER_ESP_ISP_CCM;
    ioctl(isp_fd, VIDIOC_S_EXT_CTRLS, &controls);

    // 2. Appliquer AWB via V4L2_CID_USER_ESP_ISP_AWB
    esp_video_isp_awb_t awb = {
        .enable = true,
        .rg_min = ipa_json_config->awb.rg_min,
        .rg_max = ipa_json_config->awb.rg_max,
        .bg_min = ipa_json_config->awb.bg_min,
        .bg_max = ipa_json_config->awb.bg_max,
    };
    control[0].id = V4L2_CID_USER_ESP_ISP_AWB;
    ioctl(isp_fd, VIDIOC_S_EXT_CTRLS, &controls);

    // 3. Appliquer Sharpen via V4L2_CID_USER_ESP_ISP_SHARPEN
    // 4. Appliquer Gamma
    // 5. Appliquer Contrast

    return ESP_OK;
}
```

## 🎯 Plan d'action

### Étape 1: Restaurer les anciens fichiers ✅

1. Récupérer `esp_ipa_json_loader.c` du commit avant 261ccb9
2. Récupérer `esp_ipa_detect_stubs.c` du commit avant 261ccb9
3. Récupérer `esp_ipa_json_loader.h` du commit avant 261ccb9
4. Remplacer mes nouveaux fichiers par les anciens

### Étape 2: Adapter si nécessaire

1. Vérifier si les structures V4L2 ont changé en 1.4.1
2. Adapter les noms si nécessaire
3. Tester la compilation

### Étape 3: Tester

1. Compiler et flasher
2. Vérifier que CCM/AWB/sharpen sont appliqués
3. Comparer qualité d'image avant/après

## 💡 Pourquoi c'est mieux que ma solution actuelle

### Ma solution (actuelle):
```
esp_ipa_pipeline_get_config() → retourne NULL
→ ISP/IPA complètement désactivé
→ Images brutes, couleurs fades
→ Pas de balance blancs
❌ Mauvaise qualité
```

### Ancienne solution (qui marchait):
```
esp_ipa_load_json_config() → parse JSON avec cJSON
esp_ipa_apply_json_to_isp() → applique via V4L2
→ CCM appliqué → couleurs correctes
→ AWB appliqué → balance blancs OK
→ Sharpen appliqué → netteté OK
✅ Excellente qualité (comme avant!)
```

## 🔧 Dépendances

L'ancien code nécessite:
- **cJSON**: Pour parser les JSON embarqués
  - Déjà dans ESP-IDF?
  - Sinon, peut utiliser une version minimaliste

- **V4L2 headers**: Pour les ioctl ISP
  - `linux/videodev2.h`
  - `esp_video_isp_ioctl.h`
  - Normalement fournis par esp_video

## 📝 Conclusion

Vous aviez une **solution parfaitement fonctionnelle** avant la mise à jour!

La mise à jour vers esp-video 1.4.1 a **supprimé** votre code custom qui marchait.

**Je vais restaurer l'ancien code** pour que ça refonctionne comme avant!
