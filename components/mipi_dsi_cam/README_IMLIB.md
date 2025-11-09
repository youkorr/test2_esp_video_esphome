# imlib - Bibliothèque de dessin zero-copy pour mipi_dsi_cam

## Comment ça fonctionne

`imlib` est une **bibliothèque C pure** compilée automatiquement par ESP-IDF via le `CMakeLists.txt` de `mipi_dsi_cam`.

**Vous n'avez RIEN à ajouter dans votre YAML** - `imlib` est compilé automatiquement.

## ✅ Configuration correcte

```yaml
# ============================================================================
# PAS BESOIN de déclarer imlib: - il est compilé automatiquement
# ============================================================================

esp_video:
  i2c_id: bsp_bus
  xclk_pin: GPIO36
  xclk_freq: 24000000
  enable_h264: true
  enable_jpeg: true
  enable_isp: true

mipi_dsi_cam:
  id: tab5_cam
  sensor_type: sc202cs
  pixel_format: RGB565
  resolution: 720P
  # ... reste de la config
```

## Pourquoi cette ligne est nécessaire ?

`imlib` est une bibliothèque C pure compilée via ESP-IDF CMakeLists.txt. Elle n'a pas de configuration YAML, mais ESPHome a besoin de la charger explicitement pour :
1. Reconnaître le composant dans le système de build
2. Inclure le CMakeLists.txt dans la compilation IDF
3. Rendre les headers disponibles pour `#include "imlib.h"`

## Que fait imlib ?

`imlib` fournit des fonctions de dessin optimisées pour RGB565 :
- `draw_string()` - Texte avec police Unicode 16x16
- `draw_line()` - Lignes
- `draw_rectangle()` - Rectangles
- `draw_circle()` - Cercles
- `get_pixel()` / `set_pixel()` - Accès pixel individuel

Toutes ces fonctions dessinent **directement sur le buffer V4L2** (zero-copy) sans allocation mémoire supplémentaire.

## Erreur si imlib: manque

```
Component mipi_dsi_cam requires component imlib.
```

ou

```
No module named 'esphome.components.imlib'
```

**Solution** : Ajouter `imlib:` dans le YAML comme montré ci-dessus.

## Documentation complète

Voir `/components/imlib/USAGE_ESPHOME.md` pour des exemples complets d'utilisation.
