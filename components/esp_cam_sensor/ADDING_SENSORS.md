# Ajout de capteurs SPI/DVP pour ESPHome

## Capteurs actuellement supportés

Les capteurs suivants sont configurés dans `src/esp_cam_sensor_detect_stubs.c` :

| Capteur   | Interface   | Adresse I2C |
|-----------|-------------|-------------|
| OV5647    | MIPI-CSI    | 0x36        |
| SC202CS   | MIPI-CSI    | 0x36        |
| OV02C10   | MIPI-CSI    | 0x36        |

## Ajout d'un capteur SPI

Si vous avez un capteur connecté via SPI, vous devez :

1. Ajouter le fichier source du capteur à `components/esp_video/esp_video_build.py` :
```python
esp_cam_sensor_sources = [
    # ... capteurs existants ...
    "sensor/votre_capteur_spi/votre_capteur_spi.c",
]
```

2. Ajouter la fonction de détection dans `src/esp_cam_sensor_detect_stubs.c` :
```c
#include "votre_capteur_spi.h"

// Après les capteurs MIPI-CSI existants :
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_sensor_votre_capteur = {
    .detect = votre_capteur_detect,
    .port = ESP_CAM_SENSOR_SPI,
    .sccb_addr = VOTRE_CAPTEUR_SPI_ADDR
};
```

3. Le capteur sera détecté automatiquement au boot

## Ajout d'un capteur DVP

Si votre capteur peut utiliser l'interface DVP (parallel), procédez de même mais utilisez :
```c
.port = ESP_CAM_SENSOR_DVP,
```

## Pour l'ESP32-P4

L'ESP32-P4 dispose de :
- **Encodeur H.264 matériel** : Utilisé automatiquement via `/dev/video_h264`
- **Décodeur H.264 matériel** : Disponible pour le décodage
- **Encodeur JPEG matériel** : Utilisé via `/dev/video_jpeg`
- **Pipeline ISP** : Traitement d'image matériel via `/dev/video_isp1`

Le streaming est maintenant **contrôlé manuellement** par l'utilisateur via les composants ESPHome (`mipi_dsi_cam` ou `lvgl_camera_display`). Il ne démarre plus automatiquement au boot.

## Configuration ESPHome recommandée

```yaml
esp_video:
  enable_isp: true     # Pipeline ISP pour RGB565/YUYV
  enable_jpeg: true    # Encodeur JPEG matériel
  enable_h264: true    # Encodeur H.264 matériel (ESP32-P4)

mipi_dsi_cam:
  sensor_type: SC202CS          # ou OV5647, OV02C10
  resolution: 1280x720
  pixel_format: H264            # Utilise l'encodeur matériel
  framerate: 30
  jpeg_quality: 80
```

## Devices vidéo disponibles

- `/dev/video0` : Capteur MIPI-CSI raw
- `/dev/video_isp1` : Pipeline ISP (RGB565, YUYV, etc.)
- `/dev/video_jpeg` : Encodeur JPEG matériel
- `/dev/video_h264` : Encodeur H.264 matériel (ESP32-P4)

## Notes importantes

- Le streaming ne démarre **pas automatiquement** au boot
- Utilisez les composants ESPHome pour contrôler la capture
- L'encodeur H.264 matériel est uniquement disponible sur ESP32-P4
- Les bibliothèques software (OpenH264, TinyH264) sont en backup
