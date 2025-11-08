# Configuration de Détection des Capteurs

## Comment changer l'ordre de détection des capteurs

Le fichier `components/esp_cam_sensor/src/esp_cam_sensor_detect_stubs.c` définit l'ordre dans lequel les capteurs sont testés pendant la détection automatique.

**Note**: Ce fichier est tracké par git. L'ordre actuel (SC202CS en premier) est optimisé pour M5Stack Tab5. Si vous utilisez un autre capteur, vous pouvez modifier localement ce fichier, mais vos changements seront visibles dans `git status`.

## Capteurs Supportés

- **SC202CS** (défaut M5Stack Tab5) - ID: 0xEB52, Adresse I2C: 0x36
- **OV5647** (Raspberry Pi Camera v1) - ID: 0x5647, Adresse I2C: 0x36
- **OV02C10** (OmniVision 2MP) - ID: 0x0C10, Adresse I2C: 0x36

## Ordre Actuel (Optimisé pour M5Stack Tab5)

```c
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start[] = {
    // Sensor 0: SC202CS (M5Stack Tab5 default sensor - try first!)
    {
        .detect = (esp_cam_sensor_device_t *(*)(void *))sc202cs_detect,
        .port = ESP_CAM_SENSOR_MIPI_CSI,
        .sccb_addr = SC202CS_SCCB_ADDR
    },
    // Sensor 1: OV5647
    {
        .detect = (esp_cam_sensor_device_t *(*)(void *))ov5647_detect,
        .port = ESP_CAM_SENSOR_MIPI_CSI,
        .sccb_addr = OV5647_SCCB_ADDR
    },
    // Sensor 2: OV02C10
    {
        .detect = (esp_cam_sensor_device_t *(*)(void *))ov02c10_detect,
        .port = ESP_CAM_SENSOR_MIPI_CSI,
        .sccb_addr = OV02C10_SCCB_ADDR
    },
};
```

## Comment Modifier pour Votre Capteur

### Exemple 1: Si vous utilisez OV5647

Modifiez `components/esp_cam_sensor/src/esp_cam_sensor_detect_stubs.c` pour mettre OV5647 en premier:

```c
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start[] = {
    // Sensor 0: OV5647 (Raspberry Pi Camera - try first!)
    {
        .detect = (esp_cam_sensor_device_t *(*)(void *))ov5647_detect,
        .port = ESP_CAM_SENSOR_MIPI_CSI,
        .sccb_addr = OV5647_SCCB_ADDR
    },
    // Sensor 1: SC202CS
    {
        .detect = (esp_cam_sensor_device_t *(*)(void *))sc202cs_detect,
        .port = ESP_CAM_SENSOR_MIPI_CSI,
        .sccb_addr = SC202CS_SCCB_ADDR
    },
    // Sensor 2: OV02C10
    {
        .detect = (esp_cam_sensor_device_t *(*)(void *))ov02c10_detect,
        .port = ESP_CAM_SENSOR_MIPI_CSI,
        .sccb_addr = OV02C10_SCCB_ADDR
    },
};
```

### Exemple 2: Si vous n'avez qu'un seul capteur (SC202CS)

Pour accélérer le démarrage, vous pouvez ne lister que votre capteur:

```c
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start[] = {
    // Sensor 0: SC202CS only
    {
        .detect = (esp_cam_sensor_device_t *(*)(void *))sc202cs_detect,
        .port = ESP_CAM_SENSOR_MIPI_CSI,
        .sccb_addr = SC202CS_SCCB_ADDR
    },
};
```

## Avantages de cette Approche

1. **Pas de pollution du dépôt**: Chaque utilisateur peut avoir sa propre configuration
2. **Détection plus rapide**: Le capteur le plus probable est testé en premier
3. **Logs plus clairs**: Moins de messages d'erreur pour les capteurs non présents
4. **Flexible**: Facile d'ajouter de nouveaux capteurs à l'avenir

## Vérifier la Détection

Après modification, vérifiez les logs de boot:

```
[esp_video] Calling esp_video_init()
[sc202cs] Detected Camera sensor PID=0xeb52  ← Capteur détecté avec succès!
[esp_video] ✅ /dev/video0 existe (CSI video device - capteur détecté!)
```

Si vous voyez des erreurs avant la détection réussie:
```
[ov5647] Camera sensor is not OV5647, PID=0x0  ← Normal si OV5647 n'est pas connecté
[sc202cs] Detected Camera sensor PID=0xeb52    ← Votre capteur détecté!
```

C'est normal - les capteurs sont testés dans l'ordre jusqu'à ce qu'un soit détecté.

## Ajouter un Nouveau Capteur

Pour ajouter un nouveau capteur:

1. Ajoutez le driver dans `components/esp_cam_sensor/sensor/`
2. Ajoutez l'include dans `esp_cam_sensor_detect_stubs.c`:
   ```c
   #include "mon_nouveau_capteur.h"
   ```
3. Ajoutez-le au tableau de détection:
   ```c
   {
       .detect = (esp_cam_sensor_device_t *(*)(void *))mon_nouveau_capteur_detect,
       .port = ESP_CAM_SENSOR_MIPI_CSI,
       .sccb_addr = MON_NOUVEAU_CAPTEUR_SCCB_ADDR
   },
   ```

## Notes Importantes

- **L'ordre compte**: Le premier capteur trouvé sera utilisé
- **Adresses I2C**: Tous les capteurs MIPI-CSI listés utilisent 0x36
- **XCLK requis**: Le capteur ne répondra sur I2C que si XCLK est actif (initialisé automatiquement)
- **Délai de stabilisation**: 100ms de délai après activation de XCLK permet au capteur de s'initialiser

## Dépannage

### Le capteur n'est pas détecté (PID=0x0)

1. Vérifiez que XCLK est initialisé:
   ```
   [esp_video] ✅ XCLK initialized successfully via LEDC
   ```

2. Vérifiez le délai de stabilisation:
   ```
   [esp_video] ⏳ Waiting 100ms for sensor to stabilize...
   ```

3. Vérifiez l'adresse I2C de votre capteur (0x36 pour SC202CS/OV5647/OV02C10)

4. Vérifiez le Chip ID avec le test direct:
   ```
   [esp_video] ✅ I2C lecture réussie: Chip ID = 0xEB52
   ```

### Mauvais capteur détecté

Si un mauvais capteur est détecté, changez l'ordre dans le tableau pour mettre le bon capteur en premier.

## Références

- **M5Stack Tab5**: Utilise SC202CS (0xEB52)
- **Raspberry Pi Camera v1**: Utilise OV5647 (0x5647)
- **Datasheet SC202CS**: https://github.com/espressif/esp-video/tree/main/esp_cam_sensor/sensor/sc202cs
