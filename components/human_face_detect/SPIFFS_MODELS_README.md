# Face Detection Models - SPIFFS Embedding

Les modèles de détection de visages sont embarqués automatiquement dans le firmware via une partition SPIFFS.

## Configuration Requise

### 1. Partitions personnalisées

Le fichier `partitions.csv` à la racine du projet définit une partition SPIFFS de 1MB pour les modèles :

```csv
spiffs, data, spiffs, 0x610000, 0x100000,
```

### 2. Configuration ESPHome

Ajoutez cette section dans votre fichier YAML ESPHome :

```yaml
esp32:
  board: esp32-p4-function-ev-board
  variant: esp32p4
  framework:
    type: esp-idf
    version: 5.3.1
    platform_version: 6.8.1
    sdkconfig_options:
      # Enable SPIFFS support
      CONFIG_SPIFFS_SUPPORT: y
      CONFIG_SPIFFS_MAX_PARTITIONS: "3"

  # Use custom partitions table
  partitions: partitions.csv
```

## Comment ça fonctionne

1. **Build Time** : Le CMakeLists.txt du composant `human_face_detect` crée automatiquement une image SPIFFS contenant les fichiers `.espdl` depuis `models/p4/`

2. **Runtime** : Au démarrage, `human_face_detect` monte la partition SPIFFS sur `/spiffs/` et charge les modèles depuis :
   - `/spiffs/human_face_detect_msr_s8_v1.espdl` (60 KB)
   - `/spiffs/human_face_detect_mnp_s8_v1.espdl` (127 KB)

3. **Automatic** : Aucune carte SD n'est nécessaire - les modèles sont dans le firmware !

## Vérification

Au démarrage, vous devriez voir ces logs :

```
[I][human_face_detect:132]: Mounting SPIFFS partition for embedded models...
[I][human_face_detect:164]: ✅ SPIFFS mounted successfully
[I][human_face_detect:165]:    Partition size: 1024 KB, Used: 187 KB
[I][human_face_detect:055]: Initializing ESP-DL face detection models...
[I][human_face_detect:081]: ✓ Model files found on SD card
[I][human_face_detect:088]: ✅ ESP-DL face detection initialized successfully
```

## Dépannage

### Erreur: "SPIFFS partition not found"

Solution : Assurez-vous que `partitions: partitions.csv` est défini dans votre YAML et que le fichier existe à la racine.

### Erreur: "Model file not found"

Solution : Les modèles doivent être présents dans `components/human_face_detect/models/p4/` lors de la compilation. Vérifiez que les fichiers `.espdl` existent.

### Alternative : Utiliser une carte SD

Si vous préférez utiliser une carte SD au lieu de SPIFFS, modifiez `model_dir` dans votre YAML :

```yaml
human_face_detect:
  id: face_detector
  camera: my_cam
  model_dir: "/sdcard"  # Au lieu de /spiffs
```

Puis copiez manuellement les fichiers `.espdl` sur la carte SD.
