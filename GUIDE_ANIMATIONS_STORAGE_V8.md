# Guide: Animations PNG avec Storage Component (LVGL V8)

Alternative à Lottie pour LVGL V8 - Utilise le widget `animimg` avec séquences PNG

---

## 📋 Table des Matières

1. [Préparation des Animations](#1-préparation-des-animations)
2. [Conversion Lottie → PNG](#2-conversion-lottie--png)
3. [Configuration Storage](#3-configuration-storage)
4. [Utilisation dans LVGL](#4-utilisation-dans-lvgl)
5. [Optimisation RAM](#5-optimisation-ram)
6. [Exemples Complets](#6-exemples-complets)

---

## 1. Préparation des Animations

### Structure SD Card

```
/sdcard/
├── animations/
│   ├── loading/          # Spinner de chargement (8 frames)
│   │   ├── frame_01.png
│   │   ├── frame_02.png
│   │   └── ... frame_08.png
│   ├── alarm/            # Icône alarm (6 frames)
│   │   ├── frame_01.png
│   │   └── ... frame_06.png
│   └── heart/            # Battement coeur (12 frames)
│       ├── frame_01.png
│       └── ... frame_12.png
└── img/
    └── sanctuary.jpg     # Images statiques
```

### Recommandations Taille/Format

| Type Animation | Frames | Taille | Format | RAM Usage |
|----------------|--------|--------|--------|-----------|
| Loading Spinner | 8 | 100x100 | RGB565 | 160KB |
| Icon Animation | 6 | 64x64 | RGB565 | 49KB |
| Heart Beat | 12 | 80x80 | RGB565 | 154KB |
| Small Icon | 4 | 32x32 | RGB565 | 8KB |

**Total RAM** pour les 3 exemples: ~363KB (acceptable sur ESP32-P4)

---

## 2. Conversion Lottie → PNG

### Installation

```bash
pip install lottie cairosvg pillow
```

### Utilisation du Script

Le script `tools/lottie_to_png_sequence.py` est fourni pour convertir vos fichiers Lottie:

```bash
# Conversion basique (8 frames, 100x100px)
python tools/lottie_to_png_sequence.py Sandy_Loading.json animations/loading/

# Personnalisation
python tools/lottie_to_png_sequence.py \
  Sandy_Loading.json \
  animations/loading/ \
  --frames 12 \
  --size 120
```

### Exemple avec votre "Sandy Loading.json"

```bash
cd /home/user/test2_esp_video_esphome

# Créer dossier de sortie
mkdir -p animations/sandy_loading

# Conversion (12 frames pour animation fluide)
python tools/lottie_to_png_sequence.py \
  "Sandy Loading.json" \
  animations/sandy_loading/ \
  --frames 12 \
  --size 100

# Résultat:
# animations/sandy_loading/frame_01.png
# animations/sandy_loading/frame_02.png
# ... frame_12.png
```

### Copier vers SD Card

```bash
# Monter la carte SD et copier
cp -r animations/sandy_loading /sdcard/animations/
```

---

## 3. Configuration Storage

### Configuration Complète

Votre fichier ESPHome (basé sur votre config existante):

```yaml
storage:
  platform: sd_direct

  sd_images:
    # ========== Votre image existante ==========
    - id: sanctuary
      file_path: "/img/sanctuary.jpg"
      resize: 1280x720
      format: rgb565

    # ========== Animation Sandy Loading (12 frames) ==========
    - id: sandy_f01
      file_path: "/animations/sandy_loading/frame_01.png"
      resize: 100x100
      format: rgb565
      auto_load: true

    - id: sandy_f02
      file_path: "/animations/sandy_loading/frame_02.png"
      resize: 100x100
      format: rgb565
      auto_load: true

    - id: sandy_f03
      file_path: "/animations/sandy_loading/frame_03.png"
      resize: 100x100
      format: rgb565
      auto_load: true

    # ... continuer jusqu'à frame_12

    - id: sandy_f12
      file_path: "/animations/sandy_loading/frame_12.png"
      resize: 100x100
      format: rgb565
      auto_load: true
```

### Option: Chargement Dynamique (RAM limitée)

Si vous avez peu de RAM, chargez les frames à la demande:

```yaml
storage:
  sd_images:
    - id: sandy_f01
      file_path: "/animations/sandy_loading/frame_01.png"
      resize: 100x100
      format: rgb565
      auto_load: false  # ← Ne charge PAS au démarrage
```

Puis dans automation:

```yaml
on_boot:
  - storage.load_image: sandy_f01
  - storage.load_image: sandy_f02
  # ...
```

---

## 4. Utilisation dans LVGL

### Widget animimg Basique

```yaml
lvgl:
  displays:
    - display_id: my_display
      pages:
        - id: page_home
          widgets:
            - animimg:
                id: sandy_loader
                align: CENTER
                width: 100
                height: 100
                src:
                  - sandy_f01
                  - sandy_f02
                  - sandy_f03
                  - sandy_f04
                  - sandy_f05
                  - sandy_f06
                  - sandy_f07
                  - sandy_f08
                  - sandy_f09
                  - sandy_f10
                  - sandy_f11
                  - sandy_f12
                duration: 1200        # 1200ms = 1.2s pour cycle complet
                repeat_count: -1      # Boucle infinie
                auto_start: true      # Démarre automatiquement
```

### Contrôle par Automation

```yaml
# Démarrer animation
on_some_event:
  - lvgl.animimg.start:
      id: sandy_loader

# Arrêter animation
on_other_event:
  - lvgl.animimg.stop:
      id: sandy_loader

# Démarrer avec nombre de répétitions
on_another_event:
  - lvgl.animimg.start:
      id: sandy_loader
      repeat_count: 3  # 3 cycles puis stop
```

### Exemple: Animation WiFi

```yaml
# Dans votre configuration ESPHome
wifi:
  ssid: "VotreSSID"
  password: "VotrePassword"

  on_connect:
    - lvgl.animimg.stop:
        id: sandy_loader
    - lvgl.label.update:
        id: wifi_status
        text: "Connecté!"

  on_disconnect:
    - lvgl.animimg.start:
        id: sandy_loader
        repeat_count: -1
    - lvgl.label.update:
        id: wifi_status
        text: "Connexion..."
```

---

## 5. Optimisation RAM

### Calcul RAM par Frame

```
Taille RAM = largeur × hauteur × bytes_per_pixel

RGB565: 2 bytes/pixel
- 100x100 = 20 KB/frame
- 64x64   = 8 KB/frame
- 32x32   = 2 KB/frame

L8 (grayscale): 1 byte/pixel
- 100x100 = 10 KB/frame (50% économie)
```

### Techniques d'Optimisation

#### 1. Réduire le Nombre de Frames

```bash
# 12 frames → 6 frames (50% moins RAM)
python tools/lottie_to_png_sequence.py \
  input.json output/ \
  --frames 6 \
  --size 100
```

#### 2. Réduire la Taille

```bash
# 100x100 → 64x64 (60% moins RAM)
python tools/lottie_to_png_sequence.py \
  input.json output/ \
  --frames 8 \
  --size 64
```

#### 3. Utiliser Grayscale (si possible)

Dans storage config:

```yaml
- id: sandy_f01
  file_path: "/animations/sandy_loading/frame_01.png"
  resize: 100x100
  format: l8  # ← Grayscale (1 byte/pixel au lieu de 2)
```

#### 4. Chargement/Déchargement Dynamique

```yaml
# Charger avant utilisation
on_page_load:
  - storage.load_image: sandy_f01
  - storage.load_image: sandy_f02
  # ...
  - lvgl.animimg.start: sandy_loader

# Décharger après utilisation
on_page_unload:
  - lvgl.animimg.stop: sandy_loader
  - storage.unload_image: sandy_f01
  - storage.unload_image: sandy_f02
  # ...
```

---

## 6. Exemples Complets

### Exemple 1: Page de Chargement

```yaml
lvgl:
  displays:
    - display_id: my_display
      pages:
        - id: page_loading
          bg_color: 0x000000
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
                duration: 1200
                repeat_count: -1
                auto_start: true

            # Label de progression
            - label:
                id: loading_text
                align: CENTER
                y: 80
                text: "Chargement..."
                text_color: 0xFFFFFF

# Automation de démarrage
on_boot:
  - lvgl.page.show: page_loading
  - delay: 3s
  - lvgl.page.show: page_home
```

### Exemple 2: Notification avec Animation

```yaml
lvgl:
  pages:
    - id: page_home
      widgets:
        # Animation alarm en haut à droite
        - animimg:
            id: alarm_anim
            align: TOP_RIGHT
            x: -20
            y: 20
            width: 64
            height: 64
            src: [alarm_f01, alarm_f02, alarm_f03,
                  alarm_f04, alarm_f05, alarm_f06]
            duration: 600
            repeat_count: 5
            auto_start: false

# Déclenché par sensor
binary_sensor:
  - platform: gpio
    pin: GPIO15
    name: "Door Sensor"
    on_press:
      - lvgl.animimg.start:
          id: alarm_anim
          repeat_count: 5
```

### Exemple 3: Bouton Toggle Animation

```yaml
lvgl:
  pages:
    - id: page_home
      widgets:
        # Container pour l'animation
        - obj:
            id: heart_container
            align: CENTER
            width: 120
            height: 120
            bg_opa: TRANSP
            border_width: 0
            widgets:
              - animimg:
                  id: heart_beat
                  align: CENTER
                  width: 80
                  height: 80
                  src: [heart_f01, heart_f02, heart_f03, heart_f04,
                        heart_f05, heart_f06, heart_f07, heart_f08,
                        heart_f09, heart_f10, heart_f11, heart_f12]
                  duration: 1200
                  repeat_count: -1
                  auto_start: false

        # Bouton de contrôle
        - button:
            id: btn_heart
            align: BOTTOM_MID
            y: -20
            width: 150
            height: 50
            checkable: true
            widgets:
              - label:
                  text: "Toggle ❤️"
                  align: CENTER
            on_press:
              - lvgl.animimg.start: heart_beat
            on_release:
              - lvgl.animimg.stop: heart_beat
```

---

## 🔄 Migration Future vers LVGL V9

Quand vous migrerez vers LVGL V9, vous pourrez remplacer ces PNG sequences par de vrais fichiers Lottie:

### Avant (V8 - PNG sequence):
```yaml
storage:
  sd_images:
    - id: sandy_f01
      file_path: "/animations/sandy_loading/frame_01.png"
    # ... 12 frames

animimg:
  src: [sandy_f01, sandy_f02, ..., sandy_f12]
```

### Après (V9 - Lottie natif):
```yaml
storage:
  sd_lottie_animations:
    - id: sandy_loader
      file_path: "/animations/Sandy_Loading.json"

lottie:
  src: sandy_loader
```

**Avantages V9:**
- 1 fichier JSON au lieu de 12 PNG
- Moins de RAM (vecteurs vs bitmaps)
- Animations plus fluides
- Modifications faciles (éditer JSON)

---

## 📊 Comparaison: PNG Sequence vs Lottie

| Critère | PNG Sequence (V8) | Lottie (V9) |
|---------|-------------------|-------------|
| **Compatibilité** | ✅ LVGL V8 | ❌ LVGL V9 uniquement |
| **Fichiers** | 12 PNG (1.2 MB) | 1 JSON (50 KB) |
| **RAM Usage** | 240 KB (12×20KB) | ~80 KB (vecteurs) |
| **Qualité** | Fixe (pixelisée) | Vectorielle (scalable) |
| **Modifications** | Difficile (12 fichiers) | Facile (1 JSON) |
| **Performance** | Bonne | Excellente |
| **Mise en œuvre** | Simple | Complexe (ThorVG) |

---

## 🎯 Récapitulatif

### Pour LVGL V8 (maintenant):
1. ✅ Utiliser `animimg` avec PNG sequences
2. ✅ Voir `exemples_storage_animations_v8.yaml`
3. ✅ Utiliser `tools/lottie_to_png_sequence.py` pour convertir

### Pour LVGL V9 (futur):
1. Migrer vers `external_components` lvgl-9.4
2. Activer `lvgl_advanced_features` component
3. Utiliser Lottie natif avec ThorVG

---

## 📚 Fichiers de Référence

- `exemples_storage_animations_v8.yaml` - Configuration complète
- `tools/lottie_to_png_sequence.py` - Script de conversion
- `MIGRATION_LVGL_V9_README.md` - Guide migration V9
- `lvgl_v9_thorvg_complete_config.yaml` - Config V9 complète

---

## ❓ Troubleshooting

### "Out of memory" au boot
→ Utiliser `auto_load: false` et charger dynamiquement

### Animation saccadée
→ Réduire le nombre de frames (12→6) ou augmenter `duration`

### Fichiers PNG non trouvés
→ Vérifier le chemin exact sur SD card avec `/` au début

### Widget animimg non reconnu
→ Vérifier que vous êtes sur LVGL V8 (pas V7)
