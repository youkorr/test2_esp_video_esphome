# Configuration XCLK pour Capteurs MIPI-CSI

## Problème: Chip ID = 0x0000

Vos logs montrent:
```
✅ I2C lecture réussie: Chip ID = 0x0000 (attendu: 0xEB52 pour SC202CS)
❌ ID invalide - XCLK probablement inactif
```

**Cause:** Les capteurs MIPI-CSI ont besoin d'une horloge externe (XCLK) pour fonctionner. Sans XCLK, le capteur ne répond pas sur I2C et renvoie Chip ID = 0x0000.

## Solution: Activer `enable_xclk_init`

### Pour Boards NON-M5Stack (OV5647, OV02C10)

```yaml
esp_video:
  i2c_id: i2c_bus
  enable_isp: true
  enable_jpeg: true
  enable_h264: true
  xclk_pin: GPIO36          # Pin XCLK de votre board
  xclk_freq: 24000000       # 24 MHz (standard pour MIPI-CSI)
  enable_xclk_init: true    # ⭐ ACTIVER pour boards non-M5Stack!

mipi_dsi_cam:
  sensor: "ov5647"          # ou "ov02c10"
  resolution: "1024x600"
  pixel_format: "RGB565"
  framerate: 30
```

### Pour M5Stack Tab5 (SC202CS)

```yaml
esp_video:
  i2c_id: i2c_bus
  enable_isp: true
  enable_jpeg: true
  enable_h264: true
  enable_xclk_init: false   # ⭐ DÉSACTIVER pour M5Stack (BSP initialise déjà)

mipi_dsi_cam:
  sensor: "sc202cs"
  resolution: "VGA"
  pixel_format: "RGB565"
  framerate: 30
```

## Explications

### `enable_xclk_init: true` (Boards non-M5Stack)

**Active l'initialisation XCLK via LEDC:**
1. Configure un timer LEDC pour générer l'horloge
2. Connecte le signal LEDC au GPIO spécifié (xclk_pin)
3. Attend 50ms pour que le capteur se stabilise
4. Le capteur peut alors répondre sur I2C avec son vrai Chip ID

**Résultat:**
```
🔧 Initializing XCLK for non-M5Stack board (GPIO36 @ 24000000 Hz)
✅ XCLK initialized successfully via LEDC
✅ I2C lecture réussie: Chip ID = 0x5647 (OV5647) ✓
```

### `enable_xclk_init: false` (M5Stack Tab5)

**Désactive l'initialisation XCLK:**
- M5Stack Tab5 BSP initialise déjà XCLK dans son setup
- Réinitialiser XCLK causerait des conflits et des crashes
- Le capteur fonctionne avec l'XCLK fourni par le BSP

**Résultat:**
```
ℹ️  XCLK init disabled - assuming BSP or hardware provides XCLK
✅ I2C lecture réussie: Chip ID = 0xEB52 (SC202CS) ✓
```

## Configurations par Sensor

### OV5647 (Raspberry Pi Camera V1)

```yaml
esp_video:
  xclk_pin: GPIO36          # Vérifiez votre schéma
  xclk_freq: 24000000
  enable_xclk_init: true    # ⚠️ REQUIS!

mipi_dsi_cam:
  sensor: "ov5647"
  resolution: "1024x600"    # ou "VGA" pour 640x480
  pixel_format: "RGB565"
  framerate: 30
```

**Corrections appliquées:**
- ✅ AE_TARGET = 0x36 (corrige rouge et bruit)

### SC202CS (M5Stack Tab5)

```yaml
esp_video:
  enable_xclk_init: false   # ⚠️ Ne PAS activer sur M5Stack!

mipi_dsi_cam:
  sensor: "sc202cs"
  resolution: "VGA"
  pixel_format: "RGB565"
  framerate: 30
```

**Corrections appliquées:**
- ✅ gain_def = 32 (corrige vert)
- ✅ exp_def = 0x300 (corrige surexposition)
- ✅ ANA_GAIN_PRIORITY (réduit bruit)

### OV02C10 (Omnivision 2MP)

```yaml
esp_video:
  xclk_pin: GPIO36          # Vérifiez votre schéma
  xclk_freq: 24000000
  enable_xclk_init: true    # ⚠️ REQUIS!

mipi_dsi_cam:
  sensor: "ov02c10"
  resolution: "800x480"     # ou "1280x800"
  pixel_format: "RGB565"
  framerate: 30
```

## Dépannage

### Chip ID = 0x0000 persiste

1. **Vérifiez le brochage:**
   ```bash
   # Votre schéma doit montrer quel GPIO est XCLK
   # Exemples courants: GPIO36, GPIO15, GPIO17
   ```

2. **Testez différentes pins:**
   ```yaml
   xclk_pin: GPIO15  # Essayez différentes pins
   ```

3. **Vérifiez les logs:**
   ```
   ✅ XCLK initialized successfully via LEDC  # Doit apparaître
   ```

### Crash/Reboot après activation

Si vous obtenez un crash:
1. **M5Stack?** → Désactivez `enable_xclk_init: false`
2. **Conflit de pin?** → Changez `xclk_pin` à un autre GPIO
3. **Fréquence trop élevée?** → Réduisez `xclk_freq: 20000000` (20 MHz)

## Résumé

| Board Type | enable_xclk_init | Raison |
|------------|------------------|--------|
| **M5Stack Tab5** | `false` | BSP initialise déjà XCLK |
| **Autres ESP32-P4** | `true` | XCLK doit être initialisé manuellement |

**Si Chip ID = 0x0000:** Activez `enable_xclk_init: true` et vérifiez `xclk_pin`.

**Si crash après activation:** Désactivez `enable_xclk_init: false` (BSP conflit).
