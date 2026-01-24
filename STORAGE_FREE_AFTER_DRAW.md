# Feature: `free_after_draw` - Libération Automatique de la PSRAM pour Wallpapers

## Problème Résolu

Lorsque vous chargez une image wallpaper (fond d'écran) depuis la carte SD :

### AVANT (Sans `free_after_draw`)
1. 📥 Image chargée depuis SD → PSRAM (2-4 MB)
2. 🎨 `draw_to_canvas()` → Copie vers canvas LVGL
3. ❌ **Image reste en PSRAM** → Gaspillage de mémoire
4. 💾 PSRAM occupée inutilement

**Résultat** : PSRAM gaspillée pour une image qui ne sert plus

### APRÈS (Avec `free_after_draw: true`)
1. 📥 Image chargée depuis SD → PSRAM (2-4 MB)
2. 🎨 `draw_to_canvas()` → Copie vers canvas LVGL
3. ⏱️ Attendre 100ms (LVGL rendering)
4. ✅ **Libération automatique** → PSRAM disponible
5. 💾 PSRAM libre pour d'autres usages

**Résultat** : PSRAM économisée automatiquement

## Utilisation

### Configuration YAML

```yaml
storage:
  id: my_storage
  sd_component: sd_card
  root_path: "/sdcard"
  sd_images:
    # ════════════════════════════════════════════════════
    # WALLPAPER STATIQUE - Libération automatique
    # ════════════════════════════════════════════════════
    - id: background_wallpaper
      file_path: "/images/background.jpg"
      format: RGB565
      free_after_draw: true  # ← ACTIVE la libération automatique
      # Image libérée 100ms après draw_to_canvas()

    # ════════════════════════════════════════════════════
    # GIF ANIMÉ - Garder en mémoire
    # ════════════════════════════════════════════════════
    - id: animated_logo
      file_path: "/images/logo_animated.gif"
      format: RGB565
      free_after_draw: false  # ← Ne PAS libérer (animation)
      # Frames gardés en PSRAM pour animation

    # ════════════════════════════════════════════════════
    # IMAGE UTILISÉE PLUSIEURS FOIS - Garder en mémoire
    # ════════════════════════════════════════════════════
    - id: icon_battery
      file_path: "/images/battery_icon.png"
      format: RGB565
      free_after_draw: false  # ← Ne PAS libérer (réutilisation)
      # Gardée en mémoire pour redessins fréquents
```

### Code LVGL (avec Canvas)

```yaml
lvgl:
  displays:
    - my_display

  pages:
    - id: main_page
      widgets:
        # Canvas pour le wallpaper
        - canvas:
            id: wallpaper_canvas
            width: 1024
            height: 600
            bg_color: 0x000000

        # Autres widgets par-dessus
        - label:
            text: "Hello World"
            x: 100
            y: 100

# ════════════════════════════════════════════════════
# Au démarrage : charger et afficher le wallpaper
# ════════════════════════════════════════════════════
on_boot:
  priority: -100  # Après LVGL setup
  then:
    # L'image est auto-chargée (auto_load: true par défaut)
    # Il suffit de la dessiner sur le canvas
    - lambda: |-
        // Dessiner le wallpaper sur le canvas
        auto img = id(background_wallpaper);
        auto canvas = id(wallpaper_canvas);

        if (img && img->is_loaded()) {
          ESP_LOGI("main", "Drawing wallpaper to canvas...");
          img->draw_to_canvas(canvas);

          // ✅ PSRAM sera automatiquement libérée dans 100ms
          // Pas besoin d'appeler unload_image() manuellement !
        }
```

## Timing Technique

### Pourquoi 100ms de délai ?

Le délai de **100ms** entre `draw_to_canvas()` et `unload_image()` est **CRITIQUE** :

```cpp
// Dans draw_to_canvas():
lv_obj_invalidate(canvas);  // ← Marque canvas pour redessin

// ⚠️ LVGL peut rendre de façon ASYNCHRONE !
// Si on libère immédiatement → CRASH possible

// ✅ Solution : Libération différée dans 100ms
this->pending_unload_time_ = millis() + 100;
```

**Pourquoi ce délai est nécessaire** :

1. **`lv_obj_invalidate()`** → Marque le canvas pour redessin
2. **LVGL rendering** → Peut être asynchrone/différé
3. **Si libération immédiate** → LVGL accède à mémoire libérée → **CRASH**
4. **Avec 100ms** → LVGL a le temps de finir le rendu → **SAFE**

### Implémentation

```cpp
// storage.cpp

void SdImageComponent::draw_to_canvas(lv_obj_t *canvas, int x, int y) {
  // ... copie pixel par pixel ...

  lv_obj_invalidate(canvas);  // Marque pour redessin

  // Programmer libération différée (évite crash)
  if (this->free_after_draw_ && !this->is_gif_animated_) {
    this->pending_unload_time_ = millis() + 100;  // ← 100ms
  }
}

void SdImageComponent::loop() {
  // Vérifier si libération programmée
  if (this->pending_unload_time_ > 0 && millis() >= this->pending_unload_time_) {
    ESP_LOGI(TAG, "Unloading image (delayed free_after_draw)");
    this->unload_image();  // ← Libère PSRAM
    this->pending_unload_time_ = 0;
  }

  // ... gestion animation GIF ...
}
```

## Quand Utiliser `free_after_draw: true` ?

### ✅ CAS D'USAGE RECOMMANDÉS

| Cas | Exemple | `free_after_draw` | Raison |
|-----|---------|-------------------|--------|
| **Wallpaper statique** | `background.jpg` | ✅ `true` | Dessiné une fois, plus besoin |
| **Splash screen** | `logo_startup.png` | ✅ `true` | Affiché puis changement de page |
| **Photo ponctuelle** | `photo_user.jpg` | ✅ `true` | Chargée, affichée, oubliée |

### ❌ CAS OÙ NE PAS UTILISER

| Cas | Exemple | `free_after_draw` | Raison |
|-----|---------|-------------------|--------|
| **GIF animé** | `loading.gif` | ❌ `false` | Besoin de tous les frames en mémoire |
| **Image réutilisée** | `icon_battery.png` | ❌ `false` | Redessinée fréquemment |
| **Carousel d'images** | `photo1.jpg`, `photo2.jpg` | ❌ `false` | Changement rapide, gardée en cache |

## Protection pour GIF Animés

La libération automatique **ne s'applique JAMAIS** aux GIF animés :

```cpp
// Protection automatique dans le code
if (this->free_after_draw_ && !this->is_gif_animated_) {  // ← Check GIF
  // Programmer libération seulement pour images statiques
  this->pending_unload_time_ = millis() + 100;
}
```

**Pourquoi** :
- GIF animé → Besoin de **tous les frames** en mémoire
- Si libération → Animation impossible (plus de frames)
- Protection automatique → Même avec `free_after_draw: true`

## Économies de Mémoire

### Exemple Concret : Waveshare ESP32-P4

**Configuration** :
- Display : 1024x600 RGB565
- Wallpaper : 1024x600 JPEG
- PSRAM totale : 16 MB
- PSRAM disponible : ~8 MB (après LVGL buffers)

**Avant `free_after_draw`** :
```
Wallpaper PSRAM     : 2.4 MB (1024x600x2 bytes)
LVGL canvas buffer  : 2.4 MB (copie)
──────────────────────────────────────
Total utilisé       : 4.8 MB
PSRAM disponible    : 3.2 MB  ← Peu pour vidéo/YOLO
```

**Après `free_after_draw: true`** :
```
Wallpaper PSRAM     : 0 MB (libéré !)
LVGL canvas buffer  : 2.4 MB
──────────────────────────────────────
Total utilisé       : 2.4 MB
PSRAM disponible    : 5.6 MB  ← +75% pour vidéo/YOLO !
```

**Gain** : **2.4 MB de PSRAM libérée** = +75% de mémoire disponible

## Actions Manuelles

Vous pouvez toujours libérer manuellement si besoin :

```yaml
# Action pour libérer manuellement
button:
  - platform: template
    name: "Free Wallpaper Memory"
    on_press:
      - sd_image.unload:
          id: background_wallpaper
      # Libère immédiatement la PSRAM

# Recharger plus tard si besoin
  - platform: template
    name: "Reload Wallpaper"
    on_press:
      - sd_image.load:
          id: background_wallpaper
      # Recharge depuis SD card
```

## Logs de Debug

Avec `free_after_draw: true`, vous verrez :

```
[12:34:56][I][sd_image]: Loading image from: /images/background.jpg
[12:34:56][I][sd_image]: Image loaded successfully: 1024x600, 2457600 bytes
[12:34:57][I][sd_image]: Drawing to canvas: img=1024x600, canvas=1024x600
[12:34:57][I][sd_image]: Scheduling PSRAM release after draw (will free in 100ms)
[12:34:57][I][sd_image]: Unloading image (delayed free_after_draw)
[12:34:57][I][sd_image]: Unloading image - Freeing memory:
[12:34:57][I][sd_image]:   image_buffer_: 2457600 bytes
[12:34:57][I][sd_image]:   gif_frames_: 0 frames, 0 bytes total
[12:34:57][I][sd_image]:   TOTAL PSRAM to free: 2457600 bytes (~2.34 MB)
[12:34:57][D][sd_image]: Image unloaded - PSRAM freed successfully
```

**Indicateurs de succès** :
- ✅ "Scheduling PSRAM release" → Libération programmée
- ✅ "Unloading image (delayed free_after_draw)" → Libération exécutée
- ✅ "TOTAL PSRAM to free: X MB" → Quantité libérée
- ✅ 100ms entre draw et unload → Timing correct

## Compatibilité

### ✅ Compatible

- **sd_mmc_card** : Carte SD montée via ESP-IDF
- **Images statiques** : JPEG, PNG, BMP
- **LVGL Canvas** : v8 et v9
- **ESP32 variants** : ESP32, ESP32-S3, **ESP32-P4**

### ⚠️ Limitations

- **GIF animés** : Protection automatique (ne libère jamais)
- **100ms délai** : Incompressible (sécurité LVGL)
- **Canvas seulement** : `draw_to_canvas()` uniquement

## Résumé

**Option** : `free_after_draw`
**Par défaut** : `false`
**Type** : `boolean`

**Recommandation** :
- ✅ **`true`** pour wallpapers statiques (économie PSRAM)
- ❌ **`false`** pour GIF animés et images réutilisées

**Sécurité** :
- ✅ Délai de 100ms évite crash LVGL
- ✅ Protection automatique GIF
- ✅ Reset automatique du timer

**Bénéfice** :
- 💾 Jusqu'à **2-4 MB PSRAM libérée** par image
- 🚀 Plus de mémoire pour vidéo, YOLO, etc.
- 🔄 Automatique, pas de gestion manuelle

---

**Prochaine étape** : Testez la compilation et vérifiez les logs !
