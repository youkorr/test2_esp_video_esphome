# Guide de Migration - lvgl_advanced_features → storage

## 📋 Résumé des Changements

Le composant `lvgl_advanced_features` a été **fusionné** dans le composant `storage` pour simplifier la configuration.

**Motivation**: Le composant `storage` a été créé pour économiser la PSRAM en chargeant les images/vidéos depuis la carte SD. Il est donc logique que la configuration des décodeurs LVGL soit centralisée au même endroit.

## ✅ Avantages de la Nouvelle Configuration

- **Un seul composant** au lieu de deux
- **Configuration centralisée** pour tout ce qui concerne les médias SD
- **Plus simple** à comprendre et à maintenir
- **Rétrocompatible** - l'ancien composant fonctionne toujours

## 🔄 Migration Pas-à-Pas

### Ancienne Configuration (2 composants)

```yaml
# Composant 1: Décodeurs LVGL
lvgl_advanced_features:
  # Formats d'images
  libpng: true
  libjpeg_turbo: true
  gif: true
  bmp: true

  # Graphiques vectoriels (v9)
  thorvg:
    internal: true
  svg: true
  lottie: true

  # Widgets
  qrcode: true
  barcode: true

  # Performance
  draw_sw_complex: true
  draw_sw_asm: neon
  shadow_cache_size: 256
  img_cache_size: 256

# Composant 2: Images SD
storage:
  sd_images:
    - id: photo1
      file_path: "/photos/photo1.jpg"
    - id: photo2
      file_path: "/photos/photo2.jpg"
```

### Nouvelle Configuration (1 composant unifié)

```yaml
# Configuration unifiée dans storage
storage:
  # Décodeurs LVGL (fusionné de lvgl_advanced_features)
  decoders:
    # Formats d'images
    libpng: true
    libjpeg_turbo: true
    gif: true
    bmp: true

    # Graphiques vectoriels (v9)
    thorvg:
      internal: true
    svg: true
    lottie: true

    # Widgets
    qrcode: true
    barcode: true

    # Performance
    draw_sw_complex: true
    draw_sw_asm: neon
    shadow_cache_size: 256
    img_cache_size: 256

  # Images SD (comme avant)
  sd_images:
    - id: photo1
      file_path: "/photos/photo1.jpg"
    - id: photo2
      file_path: "/photos/photo2.jpg"
```

## 📊 Table de Correspondance

| Ancienne Configuration | Nouvelle Configuration | Notes |
|------------------------|------------------------|-------|
| `lvgl_advanced_features:` | `storage:` → `decoders:` | Nouvelle section imbriquée |
| `lvgl_advanced_features:` → `libpng: true` | `storage:` → `decoders:` → `libpng: true` | Même nom |
| `lvgl_advanced_features:` → `thorvg:` | `storage:` → `decoders:` → `thorvg:` | Structure identique |
| `lvgl_advanced_features:` → `draw_sw_asm: neon` | `storage:` → `decoders:` → `draw_sw_asm: neon` | Même syntaxe |
| `storage:` → `sd_images:` | `storage:` → `sd_images:` | **Inchangé** |

## 🛠️ Instructions de Migration

### Étape 1: Sauvegarder votre configuration actuelle

```bash
cp your_device.yaml your_device.yaml.backup
```

### Étape 2: Éditer votre fichier YAML

1. **Localisez** la section `lvgl_advanced_features:` dans votre configuration
2. **Copiez** tout le contenu sous `lvgl_advanced_features:`
3. **Collez-le** dans `storage:` sous une nouvelle section `decoders:`
4. **Supprimez** la section `lvgl_advanced_features:` complète

### Étape 3: Exemple de transformation

**AVANT**:
```yaml
lvgl_advanced_features:
  libpng: true
  svg: true

storage:
  sd_images:
    - id: photo
      file_path: "/photo.jpg"
```

**APRÈS**:
```yaml
storage:
  decoders:
    libpng: true
    svg: true
  sd_images:
    - id: photo
      file_path: "/photo.jpg"
```

### Étape 4: Validation

```bash
esphome config your_device.yaml
```

Vérifiez les logs pour confirmer:
```
[INFO] Storage: Configuring LVGL decoders for SD card images
[INFO]   LibPNG: ENABLED (v8+v9)
[INFO]   SVG: ENABLED (requires LVGL v9 + ThorVG)
[INFO] Storage: LVGL decoders configuration complete!
```

### Étape 5: Compilation et Test

```bash
esphome compile your_device.yaml
esphome upload your_device.yaml
```

## 🔍 Cas Particuliers

### Cas 1: Configuration Minimale (JPEG/GIF uniquement)

**Ancienne configuration**:
```yaml
storage:
  sd_images:
    - id: photo
      file_path: "/photo.jpg"
```

**Nouvelle configuration**:
```yaml
# IDENTIQUE - Pas de changement nécessaire!
storage:
  sd_images:
    - id: photo
      file_path: "/photo.jpg"
```

**Note**: JPEG et GIF sont décodés nativement par `storage`, pas besoin de décodeurs LVGL.

### Cas 2: PNG/BMP/SVG seulement (pas d'images SD décodées par storage)

**Ancienne configuration**:
```yaml
lvgl_advanced_features:
  libpng: true
  bmp: true

lvgl:
  widgets:
    - image:
        src: "S:/images/icon.png"  # Chargé directement par LVGL
```

**Nouvelle configuration**:
```yaml
storage:
  decoders:
    libpng: true
    bmp: true

lvgl:
  widgets:
    - image:
        src: "S:/images/icon.png"  # Toujours chargé directement par LVGL
```

**Note**: Même si vous n'utilisez pas `sd_images:`, vous devez quand même configurer les décodeurs dans `storage:`.

### Cas 3: Configuration Mixte (JPEG storage + PNG LVGL)

**Ancienne configuration**:
```yaml
lvgl_advanced_features:
  libpng: true

storage:
  sd_images:
    - id: photo_jpeg
      file_path: "/photo.jpg"  # Décodé par storage

lvgl:
  widgets:
    - image:
        src: photo_jpeg  # Via storage
    - image:
        src: "S:/icon.png"  # Via LVGL
```

**Nouvelle configuration**:
```yaml
storage:
  decoders:
    libpng: true  # Pour PNG via LVGL

  sd_images:
    - id: photo_jpeg
      file_path: "/photo.jpg"  # Décodé par storage

lvgl:
  widgets:
    - image:
        src: photo_jpeg  # Via storage
    - image:
        src: "S:/icon.png"  # Via LVGL
```

## ⚠️ Erreurs Courantes

### Erreur 1: "Component 'storage' requires dependency 'lvgl'"

**Cause**: Le composant `storage` dépend maintenant de `lvgl`.

**Solution**: Assurez-vous que `lvgl:` est configuré dans votre YAML:
```yaml
lvgl:
  displays:
    - my_display
  # ... configuration LVGL ...

storage:
  decoders:
    libpng: true
```

### Erreur 2: Build flags non appliqués

**Symptôme**: Les images PNG/SVG ne s'affichent pas.

**Solution**: Vérifiez les logs de compilation:
```
[INFO] Storage: Configuring LVGL decoders for SD card images
[INFO]   LibPNG: ENABLED (v8+v9)
```

Si absent, vérifiez que `decoders:` est bien indenté sous `storage:`.

### Erreur 3: "Unknown configuration option 'decoders'"

**Cause**: Version obsolète du composant `storage`.

**Solution**: Mettez à jour le composant:
```yaml
external_components:
  - source: github://youkorr/test2_esp_video_esphome
    components:
      - storage
      - sd_mmc_card
    refresh: 1d  # Force la mise à jour
```

## 🧪 Testing

### Test 1: JPEG via Storage

```yaml
storage:
  sd_images:
    - id: test_jpeg
      file_path: "/test.jpg"

lvgl:
  widgets:
    - image:
        src: test_jpeg
```

**Attendu**: Image affichée, logs `[I][storage] JPEG image loaded`.

### Test 2: PNG via LVGL

```yaml
storage:
  decoders:
    libpng: true

lvgl:
  widgets:
    - image:
        src: "S:/test.png"
```

**Attendu**: Image affichée, logs `[I][storage] LibPNG: ENABLED`.

### Test 3: SVG (LVGL v9)

```yaml
storage:
  decoders:
    thorvg:
      internal: true
    svg: true

lvgl:
  widgets:
    - image:
        src: "S:/icon.svg"
```

**Attendu**:
- LVGL v9: Image affichée
- LVGL v8: Warning `[W][storage] SVG: REQUESTED but requires LVGL v9`

## 📚 Ressources

- [Storage README](components/storage/README.md) - Documentation complète
- [INTEGRATION_SD_LVGL_STORAGE.md](INTEGRATION_SD_LVGL_STORAGE.md) - Guide d'intégration
- [FIXES_STORAGE_MEMORY_LEAK.md](FIXES_STORAGE_MEMORY_LEAK.md) - Corrections fuites PSRAM

## ❓ FAQ

### Q: Dois-je supprimer `lvgl_advanced_features` de mon `external_components:`?

**R**: Non, pas obligatoire. Le composant existe toujours pour la rétrocompatibilité. Mais la nouvelle approche recommandée est d'utiliser `storage:` avec `decoders:`.

### Q: Puis-je utiliser les deux composants en même temps?

**R**: Techniquement oui, mais **non recommandé**. Les build flags seront dupliqués. Choisissez une approche:
- **Nouveau style**: `storage:` → `decoders:` (recommandé)
- **Ancien style**: `lvgl_advanced_features:` (rétrocompatible)

### Q: La configuration des images `sd_images:` change-t-elle?

**R**: **Non**, `sd_images:` reste identique. Seule la configuration des décodeurs LVGL a été déplacée.

### Q: Que se passe-t-il si je n'utilise pas de décodeurs LVGL?

**R**: Vous n'avez pas besoin de `decoders:`. Configuration minimale:
```yaml
storage:
  sd_images:
    - id: photo
      file_path: "/photo.jpg"
```

## 🎉 Conclusion

La migration est simple:
1. **Déplacer** `lvgl_advanced_features:` → `storage:` → `decoders:`
2. **Garder** `sd_images:` inchangé
3. **Tester** la compilation

**Un seul composant, toutes les fonctionnalités!** 🚀
