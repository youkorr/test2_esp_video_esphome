# Animations PNG avec Storage - Guide Complet (LVGL V8)

Solution complète pour utiliser des animations type Lottie sur LVGL V8 avec le composant `storage`.

---

## 🎯 Résumé Rapide

Vous êtes actuellement sur **LVGL V8** et voulez des animations fluides. Le widget **Lottie** nécessite LVGL V9, donc voici la solution V8:

- ✅ **Convertir** fichiers Lottie JSON → séquences PNG
- ✅ **Charger** les PNG via votre composant `storage`
- ✅ **Afficher** avec le widget `animimg` (compatible V8)

**RAM estimée**: 240 KB pour animation 12 frames de 100x100px (acceptable sur ESP32-P4)

---

## 📁 Fichiers Créés

### 1. Documentation

| Fichier | Description |
|---------|-------------|
| **`GUIDE_ANIMATIONS_STORAGE_V8.md`** | Guide complet pas à pas avec exemples |
| **`README_ANIMATIONS_V8.md`** | Ce fichier - Vue d'ensemble |
| **`exemples_storage_animations_v8.yaml`** | Exemples de configuration complète |
| **`page_home_avec_animations.yaml`** | Intégration dans votre page existante |

### 2. Outils de Conversion

| Fichier | Usage |
|---------|-------|
| **`tools/lottie_to_png_sequence.py`** | Convertir 1 fichier Lottie en PNG |
| **`tools/batch_convert_lottie.sh`** | Convertir plusieurs fichiers d'un coup |

### 3. Migration Future

| Fichier | Description |
|---------|-------------|
| **`MIGRATION_LVGL_V9_README.md`** | Guide migration vers LVGL V9 |
| **`lvgl_v9_thorvg_complete_config.yaml`** | Configuration V9 avec Lottie natif |
| **`components/lvgl_advanced_features/`** | Composant pour ThorVG/Lottie V9 |

---

## 🚀 Quick Start (3 étapes)

### Étape 1: Convertir votre fichier Lottie

```bash
cd /home/user/test2_esp_video_esphome

# Convertir "Sandy Loading.json" en 12 frames PNG de 100x100px
python tools/lottie_to_png_sequence.py \
  "Sandy Loading.json" \
  animations/sandy/ \
  --frames 12 \
  --size 100
```

**Résultat**: Dossier `animations/sandy/` avec `frame_01.png` → `frame_12.png`

### Étape 2: Copier sur carte SD

```bash
# Copier les animations vers /sdcard/
cp -r animations/sandy /sdcard/animations/
```

**Structure SD Card**:
```
/sdcard/
├── animations/
│   └── sandy/
│       ├── frame_01.png
│       ├── frame_02.png
│       └── ... frame_12.png
└── img/
    └── sanctuary.jpg
```

### Étape 3: Configurer dans ESPHome

Intégrer dans votre fichier `.yaml`:

```yaml
storage:
  platform: sd_direct
  sd_images:
    # Vos images existantes
    - id: sanctuary
      file_path: "/img/sanctuary.jpg"
      resize: 1280x720
      format: rgb565

    # Animation Sandy (12 frames)
    - id: sandy_f01
      file_path: "/animations/sandy/frame_01.png"
      resize: 100x100
      format: rgb565
      auto_load: true

    # ... continuer jusqu'à sandy_f12
    # (voir page_home_avec_animations.yaml pour config complète)

lvgl:
  pages:
    - id: page_home
      widgets:
        # Animation au centre
        - animimg:
            id: sandy_loader
            align: CENTER
            width: 100
            height: 100
            src: [sandy_f01, sandy_f02, sandy_f03, sandy_f04,
                  sandy_f05, sandy_f06, sandy_f07, sandy_f08,
                  sandy_f09, sandy_f10, sandy_f11, sandy_f12]
            duration: 1200        # 1.2s par cycle
            repeat_count: -1      # Boucle infinie
            auto_start: true
```

**Compiler et flasher**:
```bash
esphome compile votre_config.yaml
esphome upload votre_config.yaml
```

---

## 🔧 Conversion Batch (Plusieurs Fichiers)

Si vous avez plusieurs fichiers Lottie à convertir:

```bash
# Placer tous vos .json dans un dossier
mkdir lottie_files
cp "Sandy Loading.json" lottie_files/
cp "Alarm Animation.json" lottie_files/
# ... autres fichiers

# Conversion batch
./tools/batch_convert_lottie.sh lottie_files animations 12 100
```

**Avantages**:
- ✅ Convertit tous les fichiers d'un coup
- ✅ Génère automatiquement `storage_config_snippet.yaml`
- ✅ Noms de fichiers normalisés (espaces → underscores)

---

## 📊 Exemples d'Utilisation

### Exemple 1: Loading Spinner

```yaml
animimg:
  id: loading_spinner
  align: TOP_RIGHT
  x: -20
  y: 20
  src: [sandy_f01, sandy_f02, ..., sandy_f12]
  duration: 1200
  repeat_count: -1
  auto_start: true
```

### Exemple 2: Animation WiFi

```yaml
wifi:
  on_connect:
    - lvgl.animimg.stop: sandy_loader
  on_disconnect:
    - lvgl.animimg.start: sandy_loader
```

### Exemple 3: Overlay de Chargement

```yaml
# Écran noir avec animation centrée
- obj:
    id: loading_overlay
    width: 1280
    height: 720
    bg_color: 0x000000
    hidden: true
    widgets:
      - animimg:
          id: sandy_center
          align: CENTER
          src: [sandy_f01, ..., sandy_f12]
          duration: 1200
          repeat_count: -1

# Afficher/masquer
script:
  - id: show_loading
    then:
      - lvgl.widget.show: loading_overlay
      - lvgl.animimg.start: sandy_center
```

**Voir `exemples_storage_animations_v8.yaml` pour plus d'exemples**

---

## 💾 Optimisation RAM

### Calcul RAM par Animation

```
100x100 RGB565 × 12 frames = 240 KB
 64x64  RGB565 × 8 frames  = 65 KB
 32x32  RGB565 × 6 frames  = 12 KB
```

### Techniques d'Optimisation

#### 1. Réduire Nombre de Frames

```bash
# 12 frames → 6 frames (50% moins RAM)
python tools/lottie_to_png_sequence.py input.json output/ --frames 6 --size 100
```

#### 2. Réduire Taille

```bash
# 100x100 → 64x64 (60% moins RAM)
python tools/lottie_to_png_sequence.py input.json output/ --frames 12 --size 64
```

#### 3. Chargement Dynamique

```yaml
storage:
  sd_images:
    - id: sandy_f01
      auto_load: false  # ← Ne charge PAS au boot

script:
  - id: load_animation
    then:
      - storage.load_image: sandy_f01
      - storage.load_image: sandy_f02
      # ...
      - lvgl.animimg.start: sandy_loader
```

#### 4. Format Grayscale (si applicable)

```yaml
- id: sandy_f01
  format: l8  # ← 1 byte/pixel au lieu de 2 (50% économie)
```

---

## 🎨 Personnalisation

### Paramètres de Conversion

```bash
python tools/lottie_to_png_sequence.py input.json output/ [OPTIONS]

Options:
  --frames N    Nombre de frames (défaut: 8)
  --size N      Taille en pixels NxN (défaut: 100)
```

### Exemples:

```bash
# Animation très fluide (24 frames)
python tools/lottie_to_png_sequence.py input.json output/ --frames 24 --size 120

# Animation légère (4 frames, 32px)
python tools/lottie_to_png_sequence.py input.json output/ --frames 4 --size 32

# Icon animée (6 frames, 64px)
python tools/lottie_to_png_sequence.py input.json output/ --frames 6 --size 64
```

---

## 🔄 Migration Future vers LVGL V9

Quand vous serez prêt à migrer vers LVGL V9:

### Avant (V8 - PNG sequence):
```yaml
storage:
  sd_images:
    - id: sandy_f01...sandy_f12  # 12 fichiers PNG

animimg:
  src: [sandy_f01, ..., sandy_f12]
```

### Après (V9 - Lottie natif):
```yaml
external_components:
  - source:
      type: git
      url: https://github.com/clydebarrow/esphome
      ref: lvgl-9.4
    components: [lvgl]

lvgl_advanced_features:
  thorvg:
    internal: true
  lottie: true

storage:
  sd_lottie_animations:
    - id: sandy_loader
      file_path: "/animations/Sandy_Loading.json"  # 1 seul fichier!

lottie:
  src: sandy_loader  # Widget Lottie natif
```

**Avantages V9**:
- 📦 1 fichier JSON au lieu de 12 PNG
- 💾 Moins de RAM (vecteurs vs bitmaps)
- 🎨 Qualité vectorielle (scalable)
- ✏️ Modifications faciles

**Voir `MIGRATION_LVGL_V9_README.md` pour guide complet**

---

## 📚 Documentation Complète

### Pour Démarrer:
1. **`GUIDE_ANIMATIONS_STORAGE_V8.md`** - Guide détaillé pas à pas

### Pour Intégration:
2. **`page_home_avec_animations.yaml`** - Exemple complet avec votre page_home
3. **`exemples_storage_animations_v8.yaml`** - Multiples exemples d'animations

### Pour Migration V9:
4. **`MIGRATION_LVGL_V9_README.md`** - Guide migration LVGL V9
5. **`lvgl_v9_thorvg_complete_config.yaml`** - Configuration V9 complète

---

## 🛠️ Troubleshooting

### "Out of memory" au boot
**Problème**: Trop d'images chargées en RAM
**Solution**: Utiliser `auto_load: false` et charger dynamiquement

### Animation saccadée
**Problème**: Trop de frames ou duration trop courte
**Solution**: Réduire frames (12→6) ou augmenter duration (1200→2000)

### Fichiers PNG non trouvés
**Problème**: Chemin SD card incorrect
**Solution**: Vérifier `/` au début: `/animations/sandy/frame_01.png`

### Widget animimg non reconnu
**Problème**: Version LVGL incorrecte
**Solution**: Vérifier LVGL V8 (pas V7), voir logs de compilation

### Images pixelisées
**Problème**: Taille trop petite
**Solution**: Augmenter --size lors conversion (100→150)

---

## 📊 Comparaison: PNG vs Lottie

| Critère | PNG Sequence (V8) | Lottie (V9) |
|---------|-------------------|-------------|
| **Version LVGL** | ✅ V8 Compatible | ❌ V9 uniquement |
| **Fichiers** | 12 PNG (1.2 MB) | 1 JSON (50 KB) |
| **RAM** | 240 KB | ~80 KB |
| **Qualité** | Pixelisée | Vectorielle |
| **Édition** | Difficile | Facile |
| **Complexité** | Simple | Complexe |

---

## ✅ Checklist de Déploiement

- [ ] Installer dépendances Python: `pip install lottie cairosvg pillow`
- [ ] Convertir fichier(s) Lottie en PNG
- [ ] Copier animations vers carte SD: `/sdcard/animations/`
- [ ] Ajouter configuration `storage.sd_images` dans .yaml
- [ ] Créer widgets `animimg` dans pages LVGL
- [ ] Compiler: `esphome compile votre_config.yaml`
- [ ] Uploader: `esphome upload votre_config.yaml`
- [ ] Tester animations (boutons, automations)
- [ ] Ajuster duration/frames selon besoins
- [ ] Optimiser RAM si nécessaire

---

## 🎯 Résumé

### Vous Avez Maintenant:

1. ✅ **Outils de conversion** Lottie → PNG
2. ✅ **Exemples complets** de configuration
3. ✅ **Intégration** avec votre page_home
4. ✅ **Scripts automatisés** pour conversion batch
5. ✅ **Guide migration** vers LVGL V9 (futur)

### Fichiers à Consulter:

- **Démarrage rapide**: `GUIDE_ANIMATIONS_STORAGE_V8.md`
- **Intégration**: `page_home_avec_animations.yaml`
- **Exemples**: `exemples_storage_animations_v8.yaml`
- **Migration V9**: `MIGRATION_LVGL_V9_README.md`

---

## 📞 Support

### Questions Fréquentes:
Voir section Troubleshooting dans `GUIDE_ANIMATIONS_STORAGE_V8.md`

### Performance:
Voir `OPTIMISATIONS_CAMERA_VIDEO.md` pour optimisations globales

### Migration V9:
Voir `MIGRATION_LVGL_V9_README.md` pour Lottie natif

---

**Créé le**: $(date)
**Pour**: ESPHome LVGL V8 avec composant Storage
**ESP32-P4** avec carte SD

---

🎉 **Vous êtes prêt à utiliser des animations fluides sur LVGL V8!**
