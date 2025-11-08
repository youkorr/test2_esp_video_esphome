# Configuration XCLK pour ESP-Video

## Contexte

Les capteurs MIPI-CSI (SC202CS, OV5647, OV02C10) **ont absolument besoin** d'une horloge externe (XCLK) pour fonctionner. Sans XCLK active, ils ne peuvent même pas répondre sur le bus I2C pendant la détection.

Il existe deux types de cartes ESP32-P4 avec des configurations différentes:

## Type 1: Carte avec GPIO XCLK (Exemple: GPIO36)

Ces cartes utilisent une GPIO de l'ESP32-P4 pour générer l'horloge via LEDC/PWM et la fournir au capteur.

### Configuration YAML:
```yaml
esp_video:
  i2c_id: bsp_bus
  xclk_pin: GPIO36          # Format ESPHome: "GPIOXX" (ou juste 36 fonctionne aussi)
  xclk_freq: 24000000       # Fréquence: 24 MHz (typique pour MIPI-CSI)
  enable_h264: true
  enable_jpeg: true
  enable_isp: true
```

### Caractéristiques:
- ✅ Flexible: peut changer la fréquence par logiciel
- ✅ Pas de composant externe requis
- ⚠️ Requiert une GPIO disponible

### Cartes concernées:
- ESP32-P4-Function-EV-Board (GPIO36)
- Certaines cartes de développement avec XCLK configurable

## Type 2: Carte avec Oscillateur Externe sur PCB

Ces cartes ont un oscillateur/cristal soudé sur le PCB qui fournit directement 24MHz au capteur, sans passer par une GPIO de l'ESP32-P4.

### Configuration YAML:
```yaml
esp_video:
  i2c_id: bsp_bus
  xclk_pin: -1              # -1 = Pas de GPIO (oscillateur externe)
  xclk_freq: 24000000       # Fréquence de l'oscillateur sur le PCB
  enable_h264: true
  enable_jpeg: true
  enable_isp: true
```

### Caractéristiques:
- ✅ Pas de GPIO utilisée
- ✅ Horloge toujours stable
- ⚠️ Fréquence fixée par le cristal (non modifiable par logiciel)
- ⚠️ Composant externe requis sur le PCB

### Cartes concernées:
- Certaines cartes custom/industrielles
- Cartes optimisées avec oscillateur intégré

## Comment Identifier Votre Type de Carte?

### Inspection Visuelle:
1. **Regardez près du connecteur caméra** sur votre PCB
2. Cherchez un petit composant métallique rectangulaire (cristal/oscillateur)
3. Si présent → Type 2 (oscillateur externe)
4. Si absent → Type 1 (GPIO XCLK)

### Schéma Électrique:
- Consultez le schéma de votre carte
- Trouvez la ligne XCLK du capteur
- Si connectée à une GPIO ESP32-P4 → Type 1
- Si connectée à un oscillateur/cristal → Type 2

### Test Empirique:
1. Essayez d'abord avec `xclk_pin: GPIO36` (Type 1)
2. Si la détection échoue, essayez `xclk_pin: -1` ou `xclk_pin: NO_CLOCK` (Type 2)
3. Vérifiez les logs pour voir si le capteur est détecté

### Formats Acceptés pour xclk_pin:
- `GPIO36` - Format ESPHome standard (recommandé)
- `36` - Numéro GPIO simple (fonctionne aussi)
- `-1` - Pas de GPIO, oscillateur externe
- `NO_CLOCK` - Alias pour -1 (plus lisible)

## Logs Attendus

### Avec GPIO XCLK (xclk_pin: 36):
```
[ESP-Video] Configuration XCLK: GPIO36 @ 24.0 MHz
[ESP-Video]   → Horloge externe contrôlée par GPIO (pour cartes avec XCLK GPIO)
```

### Avec Oscillateur Externe (xclk_pin: -1):
```
[ESP-Video] Configuration XCLK: Oscillateur externe sur PCB @ 24.0 MHz
[ESP-Video]   → Pas de contrôle GPIO (xclk_pin=-1, pour cartes avec oscillateur intégré)
```

## Dépannage

### Problème: Capteur Non Détecté
```
E (xxx) esp_video:   ✗ Sensor detection failed for address 0x36
```

**Solutions:**
1. Vérifiez que vous utilisez le bon type de configuration (Type 1 vs Type 2)
2. Si Type 1: Vérifiez que la GPIO est correcte (pas déjà utilisée ailleurs)
3. Si Type 2: Vérifiez que l'oscillateur externe est bien 24MHz
4. Vérifiez les connexions I2C (SDA/SCL)

### Problème: Mauvaise Qualité d'Image
Si le capteur est détecté mais l'image est mauvaise:
- Vérifiez que `xclk_freq` correspond à la fréquence réelle
- Les capteurs MIPI-CSI fonctionnent généralement à 24MHz
- Certains peuvent supporter 12MHz ou 48MHz (consultez la datasheet)

## Notes Importantes

⚠️ **CRITICAL**: L'horloge XCLK **DOIT** être active **AVANT** la détection du capteur!
- Si `xclk_pin` n'est pas correctement configuré, la détection échouera
- Le capteur ne répondra même pas sur I2C sans XCLK

⚠️ **Actuellement**: Les macros `SC202CS_ENABLE_OUT_XCLK` sont vides dans le driver
- Cela signifie que le code ne génère pas encore l'horloge via LEDC
- Pour le moment, l'horloge doit être fournie par un oscillateur externe
- Une future implémentation ajoutera le support LEDC pour Type 1

## Exemple Complet de Configuration

```yaml
# Bus I2C partagé
i2c:
  id: bsp_bus
  sda: GPIO8
  scl: GPIO9
  frequency: 400kHz

# Composant ESP-Video (initialise la détection des capteurs)
esp_video:
  i2c_id: bsp_bus

  # Pour cartes AVEC GPIO XCLK (changez le numéro si nécessaire):
  xclk_pin: GPIO36

  # Pour cartes AVEC oscillateur externe (décommentez):
  # xclk_pin: -1
  # OU
  # xclk_pin: NO_CLOCK

  xclk_freq: 24000000
  enable_h264: true
  enable_jpeg: true
  enable_isp: true

# Composant Caméra
mipi_dsi_cam:
  id: my_camera
  i2c_id: bsp_bus
  sensor: sc202cs
  resolution: "720P"
  pixel_format: "RGB565"
  framerate: 30
```

## Référence des Capteurs Supportés

| Capteur | Adresse I2C | XCLK Typique | Interface |
|---------|------------|--------------|-----------|
| SC202CS | 0x36       | 24 MHz       | MIPI-CSI  |
| OV5647  | 0x36       | 24 MHz       | MIPI-CSI  |
| OV02C10 | 0x36       | 24 MHz       | MIPI-CSI  |
