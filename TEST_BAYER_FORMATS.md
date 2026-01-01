# Test des Formats Bayer pour SC202CS

## Problème : Couleurs en négatif

Si les couleurs apparaissent en négatif, le format Bayer est probablement incorrect.

## Solution : Tester les 4 formats

Modifiez `components/esp_cam_sensor/sc202cs_custom_formats.h` ligne 150 :

### Test 1 : BGGR (actuel - M5Stack)
```c
.bayer_type = ESP_CAM_SENSOR_BAYER_BGGR,  // Pattern: Blue-Green-Green-Red
```

### Test 2 : RGGB
```c
.bayer_type = ESP_CAM_SENSOR_BAYER_RGGB,  // Pattern: Red-Green-Green-Blue
```

### Test 3 : GRBG
```c
.bayer_type = ESP_CAM_SENSOR_BAYER_GRBG,  // Pattern: Green-Red-Blue-Green
```

### Test 4 : GBRG
```c
.bayer_type = ESP_CAM_SENSOR_BAYER_GBRG,  // Pattern: Green-Blue-Red-Green
```

## Modification à faire

**Fichier 1**: `components/esp_cam_sensor/sc202cs_custom_formats.h:150`
```c
static const esp_cam_sensor_isp_info_t sc202cs_800x600_isp_info = {
    .isp_v1_info = {
        .version = SENSOR_ISP_INFO_VERSION_DEFAULT,
        .pclk = 72000000,
        .hts = 1920,
        .vts = 1250,
        .exp_def = 0x4dc,
        .gain_def = 0,
        .bayer_type = ESP_CAM_SENSOR_BAYER_RGGB,  // <-- CHANGEZ ICI
    }
};
```

**Fichier 2**: `components/esp_cam_sensor/sensor/sc202cs/sc202cs.c` (5 endroits)
- Ligne 920 (800x600)
- Ligne 931 (1280x720)
- Ligne 942 (1600x1200 RAW8)
- Ligne 953 (1600x1200 RAW10)
- Ligne 964 (1600x900 RAW10)

Changez toutes les occurrences :
```c
.bayer_type = ESP_CAM_SENSOR_BAYER_RGGB,  // <-- TEST RGGB
```

## Tableau de diagnostic

| Format Bayer | Résultat attendu |
|--------------|------------------|
| BGGR (actuel) | Couleurs en négatif ❌ |
| **RGGB** | **À tester** ⏳ |
| GRBG | À tester ⏳ |
| GBRG | À tester ⏳ |

## Après chaque test

1. Recompilez : `esphome compile esp32-p4_test.yaml`
2. Flashez : `esphome upload esp32-p4_test.yaml`
3. Vérifiez les couleurs sur l'écran
4. Notez le résultat dans le tableau ci-dessus

## Format correct trouvé ?

Une fois que vous avez trouvé le bon format Bayer (couleurs normales), **utilisez-le partout** :
- `sc202cs_custom_formats.h`
- `sc202cs.c` (5 occurrences)

Cela garantira des couleurs cohérentes pour tous les modes de résolution du SC202CS.
