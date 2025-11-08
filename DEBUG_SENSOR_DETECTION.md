# Guide de Débogage - Détection Capteur SC202CS

## Situation Actuelle

### ✅ Ce qui fonctionne:
- XCLK initialisé correctement via LEDC (GPIO 36 @ 24 MHz)
- Capteur répond sur I2C: **Chip ID = 0xEB52** (correct pour SC202CS!)
- ISP Pipeline initialisé
- Pas de crash mémoire (30 MB RAM libre)

### ❌ Problème:
- **Le capteur n'est PAS détecté par `esp_video_init()`**
- `/dev/video0` n'existe pas (device CSI non créé)
- Crash réseau: `ESP_ERR_NO_MEM` lors de `esp_vfs_lwip_sockets_register`

## Diagnostic

### Observation Critique

Dans les logs, **AUCUN message de tentative de détection** n'apparaît:

**Logs précédents** (avec tentative):
```
E (298) ov5647: Camera sensor is not OV5647, PID=0x0
E (298) esp_video_init:   ✗ Sensor detection failed
```

**Logs actuels** (pas de tentative visible):
```
I (2320) ISP: 📸 IPA Pipeline created...  ← Directement ISP!
```

Cela suggère que:
1. La boucle de détection dans `esp_video_init()` ne s'exécute PAS
2. OU le fichier `esp_cam_sensor_detect_stubs.c` n'est PAS recompilé

### Test Qui Prouve Que Le Capteur Fonctionne

Notre test I2C direct (APRÈS `esp_video_init()`) **RÉUSSIT**:
```
[esp_video] ✅ I2C lecture réussie: Chip ID = 0xEB52
[esp_video]    ✅ SC202CS identifié correctement - XCLK fonctionne!
```

Donc:
- ✅ XCLK est actif
- ✅ Capteur alimenté
- ✅ I2C fonctionne
- ✅ Capteur répond correctement

**Mais `esp_video_init()` ne le détecte quand même pas!**

## Cause Probable

Le fichier **`components/esp_cam_sensor/src/esp_cam_sensor_detect_stubs.c`** qui définit l'ordre de détection des capteurs **n'est probablement PAS recompilé** malgré nos modifications.

PlatformIO/ESP-IDF cache agressivement les fichiers `.o` compilés, et même `esphome clean` ne les supprime pas toujours.

## Solutions à Essayer

### Solution 1: Clean Complet et Rebuild

```bash
# Supprimer TOUS les caches de build
rm -rf .esphome/build/
rm -rf .pioenvs/
rm -rf .pio/

# Rebuild complet
esphome clean tab5.yaml
esphome compile tab5.yaml
```

### Solution 2: Forcer la Recompilation

Si Solution 1 ne fonctionne pas, essayez de modifier manuellement le fichier:

```bash
# Ouvrez le fichier
nano components/esp_cam_sensor/src/esp_cam_sensor_detect_stubs.c

# Ajoutez un espace ou commentaire quelque part
# Sauvegardez et quittez
# Puis rebuild
```

### Solution 3: Vérifier Que Le Bon Ordre Est Compilé

Après le build, vérifiez les logs de compilation. Vous devriez voir:
```
[ESP-Video Build] + esp_cam_sensor/src/esp_cam_sensor_detect_stubs.c
```

Si ce fichier n'apparaît PAS dans les logs de build, il n'est pas recompilé!

### Solution 4: Augmenter Le Délai (Déjà Fait)

J'ai augmenté le délai de stabilisation à **300ms** au lieu de 100ms. Certains capteurs nécessitent plus de temps après l'activation de XCLK.

## Pourquoi Le Crash Réseau?

Le crash `ESP_ERR_NO_MEM` dans `esp_vfs_lwip_sockets_register` est causé par:

1. Le capteur N'est PAS détecté → `/dev/video0` non créé
2. ISP Pipeline initialisé **SANS capteur connecté** (état incohérent)
3. `mipi_dsi_cam` essaie de configurer l'ISP → `ioctl(VIDIOC_S_FMT)` échoue
4. Les drivers ESP-IDF (ISP/JPEG/H264) sont dans un état bizarre
5. Possibles fuites de file descriptors internes
6. Quand le réseau essaie de s'initialiser → manque de FDs disponibles → crash

**Solution**: Faire fonctionner la détection du capteur résoudra le crash réseau.

## Vérifications à Faire

### 1. Vérifier que SC202CS est en premier dans detect_stubs.c

Le fichier devrait contenir (dans cet ordre):
```c
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start[] = {
    // Sensor 0: SC202CS (M5Stack Tab5 default sensor - try first!)
    {
        .detect = (esp_cam_sensor_device_t *(*)(void *))sc202cs_detect,
        .port = ESP_CAM_SENSOR_MIPI_CSI,
        .sccb_addr = SC202CS_SCCB_ADDR  // 0x36
    },
    // Sensor 1: OV5647
    // ...
};
```

### 2. Vérifier les logs de build

Cherchez dans les logs de compilation:
```
[ESP-Video Build] + esp_cam_sensor/src/esp_cam_sensor_detect_stubs.c
[ESP-Video Build] + esp_cam_sensor/sensor/sc202cs/sc202cs.c
[ESP-Video Build] + esp_cam_sensor/sensor/ov5647/ov5647.c
```

Si ces lignes n'apparaissent PAS, les fichiers ne sont pas compilés!

### 3. Vérifier la taille du binaire

Après rebuild, la taille du binaire devrait changer si les fichiers ont été recompilés.

```bash
ls -lh .pioenvs/tab5/firmware.bin
```

Notez la taille avant et après le rebuild. Si identique → pas de recompilation!

## Ce Que Je M'Attends à Voir Dans Les Logs

Après un rebuild complet avec `esp_cam_sensor_detect_stubs.c` correctement recompilé:

```
[esp_video] ⏳ Waiting 300ms for sensor to stabilize...
[esp_video] ✅ Sensor should be ready for I2C communication
[esp_video] Calling esp_video_init()

[sc202cs] Detected Camera sensor PID=0xeb52  ← ✅ DEVRAIT APPARAÎTRE!

[esp_video] ✅ esp_video_init() réussi
[esp_video] 🔍 Vérification des devices vidéo créés:
[esp_video]    ✅ /dev/video0 existe (CSI video device - capteur détecté!)  ← ✅ DEVRAIT EXISTER!
[esp_video]    ✅ /dev/video10 existe (JPEG encoder)
[esp_video]    ✅ /dev/video11 existe (H.264 encoder)
[esp_video]    ✅ /dev/video20 existe (ISP device)

[esp_video] ✅ I2C lecture réussie: Chip ID = 0xEB52
[esp_video]    ✅ SC202CS identifié correctement

[mipi_dsi_cam] Ouvert: /dev/video20 (fd=6)
[mipi_dsi_cam] ISP S_FMT: 1280x720 FOURCC=0x...  ← ✅ DEVRAIT RÉUSSIR!
```

## Commits Récents

- `896b179` - Increase sensor stabilization delay to 300ms
- `eccecbb` - Force rebuild of esp_cam_sensor_detect_stubs.c with SC202CS first
- `69eafe3` - Fix delay() compilation error and add sensor configuration system
- `0ad0595` - Fix sensor detection timing and order

## Prochaine Étape

**Faites un clean complet et rebuild**, puis partagez les nouveaux logs de boot complets.

Si le problème persiste, nous devrons investiguer pourquoi la boucle de détection dans `esp_video_init.c` ne s'exécute pas ou ne trouve pas les capteurs.
