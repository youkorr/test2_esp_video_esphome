# Guide de Migration - Configuration mipi_dsi_cam

## Changements de Configuration

La configuration du composant `mipi_dsi_cam` a été mise à jour. Voici les modifications à effectuer:

### Ancien Format (INVALIDE)
```yaml
mipi_dsi_cam:
  id: tab5_cam
  i2c_id: bsp_bus
  sensor: sc202cs                # ❌ Option invalide
  external_clock_pin: GPIO36      # ❌ Option invalide
  frequency: 24000000             # ❌ Option invalide
  resolution: 720P
  pixel_format: RGB565
  framerate: 30
  jpeg_quality: 10
  mirror_x: False                 # ❌ Option invalide
  mirror_y: False                 # ❌ Option invalide
  rotation: 0                     # ❌ Option invalide
```

### Nouveau Format (VALIDE)
```yaml
mipi_dsi_cam:
  id: tab5_cam
  i2c_id: bsp_bus
  sensor_type: sc202cs            # ✅ Nouveau nom
  xclk_pin: GPIO36                # ✅ Nouveau nom
  xclk_freq: 24000000             # ✅ Nouveau nom
  sensor_addr: 0x36               # ✅ Optionnel (défaut selon capteur)
  resolution: 720P
  pixel_format: RGB565
  framerate: 30
  jpeg_quality: 10
  # mirror_x, mirror_y, rotation supprimés de la config YAML
```

## Table de Correspondance

| Ancien Nom | Nouveau Nom | Notes |
|------------|-------------|-------|
| `sensor` | `sensor_type` | Types: "sc202cs", "ov5647", "ov02c10" |
| `external_clock_pin` | `xclk_pin` | Pin GPIO (défaut: GPIO36) |
| `frequency` | `xclk_freq` | Fréquence en Hz (défaut: 24000000) |
| `mirror_x` | *supprimé* | Géré au niveau du driver |
| `mirror_y` | *supprimé* | Géré au niveau du driver |
| `rotation` | *supprimé* | Géré au niveau du driver |

## Options Disponibles

### sensor_type
- **Type**: string
- **Défaut**: "sc202cs"
- **Valeurs**: "sc202cs", "ov5647", "ov02c10"

### xclk_pin
- **Type**: string (GPIO pin)
- **Défaut**: "GPIO36"

### xclk_freq
- **Type**: entier (Hz)
- **Défaut**: 24000000 (24 MHz)

### sensor_addr
- **Type**: hexadécimal (0xXX)
- **Défaut**: 0x36 (varie selon le capteur)
- **SC202CS**: 0x36 ou 0x30
- **OV5647**: 0x36
- **OV02C10**: 0x3C

### resolution
- **Type**: string
- **Valeurs**: "720P", "1080P", etc.
- **Défaut**: "720P"

### pixel_format
- **Type**: string
- **Valeurs**: "RGB565", "JPEG"
- **Défaut**: "JPEG"

### framerate
- **Type**: entier
- **Plage**: 1-60
- **Défaut**: 30

### jpeg_quality
- **Type**: entier
- **Plage**: 1-63 (plus bas = meilleure qualité)
- **Défaut**: 10
- **Note**: Utilisé uniquement si `pixel_format: JPEG`

## Actions à Effectuer

1. **Ouvrir** `/config/esphome/tab5.yaml`
2. **Localiser** la section `mipi_dsi_cam` (ligne ~1340)
3. **Remplacer** les anciens noms par les nouveaux:
   - `sensor` → `sensor_type`
   - `external_clock_pin` → `xclk_pin`
   - `frequency` → `xclk_freq`
4. **Supprimer** les lignes:
   - `mirror_x`
   - `mirror_y`
   - `rotation`
5. **Sauvegarder** le fichier
6. **Valider** avec `esphome config tab5.yaml`

## Exemple Complet

Voir le fichier `mipi_dsi_cam_config_example.yaml` pour un exemple complet et commenté.
