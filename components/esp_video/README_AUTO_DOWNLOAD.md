# ESP-Video Auto-Download - Comme LVGL 9.4 🚀

## ✨ C'est quoi ?

ESP-Video télécharge et installe maintenant **automatiquement** toutes ses dépendances pendant la compilation, exactement comme LVGL 9.4 !

### Avant (l'ancien système) 😞

```bash
# Vous deviez faire ça manuellement :
git clone https://github.com/espressif/esp-adf-libs.git
cp -r esp-adf-libs/esp_h264 components/
cp -r esp-adf-libs/esp_cam_sensor components/
cp -r esp-adf-libs/esp_ipa components/
cp -r esp-adf-libs/esp_sccb_intf components/
# ... et ainsi de suite
```

### Après (le nouveau système) 😍

```yaml
# Vous écrivez juste ça dans votre .yaml :
esp_video:
  i2c_id: i2c_bus
  enable_h264: true
  enable_jpeg: true

# BOOM ! 🎉
# ESPHome télécharge TOUT automatiquement pendant la compilation !
```

## 🎯 Comment ça marche ?

### Système comme LVGL

**LVGL 9.4** :
```python
# Dans components/lvgl/__init__.py
cg.add_library("lvgl/lvgl", "9.4.0")  # Télécharge auto depuis le registry
```

**ESP-Video** :
```python
# Dans components/esp_video/__init__.py
ensure_esp_video_dependencies(components_dir)  # Télécharge auto depuis GitHub
```

### Workflow Automatique

Quand vous compilez votre configuration ESPHome :

1. **ESPHome démarre la compilation** de votre config YAML

2. **ESP-Video vérifie les dépendances** :
   - `esp_h264` présent ? ✅ OK / ❌ Télécharger
   - `esp_cam_sensor` présent ? ✅ OK / ❌ Télécharger
   - `esp_ipa` présent ? ✅ OK / ❌ Télécharger
   - `esp_sccb_intf` présent ? ✅ OK / ❌ Télécharger

3. **Téléchargement automatique** (si nécessaire) :
   ```
   📥 Downloading esp_h264...
      Cloning esp-adf-libs to cache...
      ✓ Repository cloned to cache
      Copying esp_h264...
      ✓ Copied successfully
   ✅ esp_h264 ready

   📥 Downloading esp_cam_sensor...
      Using cached repository
      Copying esp_cam_sensor...
      ✓ Copied successfully
   ✅ esp_cam_sensor ready

   ... (etc pour tous les composants)
   ```

4. **Compilation normale** avec tous les composants présents ✅

## 📦 Dépendances Téléchargées

ESP-Video télécharge automatiquement depuis [esp-adf-libs](https://github.com/espressif/esp-adf-libs) :

| Composant | Description | Taille |
|-----------|-------------|--------|
| **esp_h264** | Encodeur/décodeur H.264 (OpenH264 + TinyH264) | ~15 MB |
| **esp_cam_sensor** | Drivers caméra (OV5647, SC202CS, OV02C10) | ~8 MB |
| **esp_ipa** | Image Processing Algorithms (AWB, denoise, sharpen, gamma, CC) | ~12 MB |
| **esp_sccb_intf** | Interface I2C/SCCB pour communication avec les caméras | ~1 MB |

**Total** : ~36 MB (téléchargés une seule fois, puis mis en cache)

## 🗂️ Cache Intelligent

Les dépendances sont téléchargées dans un cache local :

```
~/.esphome/esp_video_cache/
├── esp-adf-libs/              # Clone Git du repo (partagé)
│   ├── esp_h264/
│   ├── esp_cam_sensor/
│   ├── esp_ipa/
│   └── esp_sccb_intf/
└── download_state.json        # État des téléchargements
```

### Avantages du cache :

- ✅ **Téléchargement unique** : Le repo n'est cloné qu'une seule fois
- ✅ **Builds rapides** : Les composants sont copiés depuis le cache (< 1 seconde)
- ✅ **Gestion des versions** : State tracking pour détecter les mises à jour
- ✅ **Partagé entre projets** : Un seul cache pour tous vos projets ESPHome

## 🔄 Mise à Jour des Dépendances

Si vous voulez mettre à jour vers la dernière version des composants :

```bash
# Option 1: Nettoyer le cache (tout re-télécharger)
rm -rf ~/.esphome/esp_video_cache

# Option 2: Nettoyer juste le state (forcer la vérification)
rm ~/.esphome/esp_video_cache/download_state.json

# Option 3: Utiliser le script Python
cd components/esp_video
python3 esp_video_download.py clean
```

Ensuite, recompilez votre projet - les nouvelles versions seront téléchargées automatiquement.

## 📝 Configuration Optionnelle

Par défaut, ESP-Video télécharge automatiquement. Si vous voulez **désactiver** l'auto-download :

```python
# Dans votre configuration ESPHome, ajoutez :
# (Ce n'est généralement PAS nécessaire)

esp_video:
  i2c_id: i2c_bus
  # ... vos options ...

  # Les composants doivent être présents localement
  # L'auto-download est ignoré si les composants existent déjà
```

## 🐛 Debug et Troubleshooting

### Voir les logs de téléchargement

```bash
# Compiler avec logs debug
esphome compile your-config.yaml --verbose
```

Vous verrez :
```
[ESP-Video Auto-Download] ========================================
[ESP-Video Auto-Download] 📦 esp_h264: Encodeur/décodeur H.264
[ESP-Video Auto-Download]    ✓ Already downloaded (cached)
[ESP-Video Auto-Download] 📦 esp_cam_sensor: Drivers caméra
[ESP-Video Auto-Download]    📥 Downloading...
[ESP-Video Auto-Download]    ✅ esp_cam_sensor ready
...
```

### Erreur : Git non installé

```
❌ Failed to download esp_h264
   Error: git: command not found
```

**Solution** :
```bash
# Ubuntu/Debian
sudo apt-get install git

# macOS
brew install git

# Windows
# Installer Git depuis https://git-scm.com
```

### Erreur : Permission denied

```
❌ Failed to download: Permission denied
```

**Solution** :
```bash
# Vérifier les permissions du cache
ls -la ~/.esphome/esp_video_cache

# Changer le propriétaire si nécessaire
sudo chown -R $USER:$USER ~/.esphome/esp_video_cache
```

### Mode Offline (pas de connexion Internet)

Si vous n'avez pas de connexion Internet, l'auto-download échouera, mais :

- ✅ Si les composants sont **déjà dans le cache** → Ça marche !
- ✅ Si les composants sont **déjà dans le dépôt local** → Ça marche !
- ❌ Si les composants sont **manquants ET pas de cache** → Échec

**Solution pour mode offline** :
1. Téléchargez les composants une fois avec Internet
2. Le cache sera utilisé pour les builds suivants (même sans Internet)

## 🎬 Exemple Complet

```yaml
# my-camera.yaml

esphome:
  name: my-camera
  friendly_name: My Camera

esp32:
  board: esp32-p4
  framework:
    type: esp-idf  # ESP-Video nécessite ESP-IDF

i2c:
  - id: i2c_bus
    sda: GPIO8
    scl: GPIO9
    frequency: 400kHz

# MAGIC ! ESP-Video télécharge tout automatiquement :
# - esp_h264 (encodeur H.264)
# - esp_cam_sensor (OV5647, SC202CS, OV02C10)
# - esp_ipa (traitement d'image)
# - esp_sccb_intf (communication I2C caméra)

esp_video:
  i2c_id: i2c_bus
  enable_h264: true      # Encodeur H.264 hardware (ESP32-P4)
  enable_jpeg: true      # Encodeur JPEG hardware
  enable_isp: true       # Image Signal Processor
  xclk_pin: GPIO36       # Clock pour le capteur caméra
  xclk_freq: 24000000    # 24 MHz

# Compilation :
# esphome run my-camera.yaml
#
# La première fois :
# - Télécharge esp-adf-libs (~36 MB) → 30 secondes
# - Copie les composants nécessaires → 5 secondes
# - Compile le firmware → 2-3 minutes
#
# Les fois suivantes (cache) :
# - Utilise le cache → instantané
# - Compile le firmware → 2-3 minutes
```

## ⚡ Performance

| Opération | Première fois | Avec cache |
|-----------|--------------|------------|
| Clone esp-adf-libs | 30s | 0s (skip) |
| Copie des composants | 5s | 2s |
| Compilation firmware | 2-3min | 2-3min |
| **TOTAL** | **~3min 35s** | **~2-3min** |

Le cache réduit le temps de setup de **35 secondes à quasi-instantané** ! 🚀

## 🎉 Résultat Final

Avec ce système, ESP-Video fonctionne **exactement comme LVGL 9.4** :

- ✅ **Zéro configuration manuelle**
- ✅ **Téléchargement automatique**
- ✅ **Cache intelligent**
- ✅ **Builds rapides**
- ✅ **Mises à jour faciles**

Profitez de votre caméra ESP32-P4 avec H.264 + ISP, sans configuration ! 📹✨

---

**Note** : Ce système est inspiré de LVGL 9.4 qui utilise `cg.add_library("lvgl/lvgl", "9.4.0")` pour télécharger automatiquement depuis le registry. ESP-Video fait la même chose, mais depuis GitHub au lieu d'un registry.
