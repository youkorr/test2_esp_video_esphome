# Fix: LVGL 9.4 Image Component Compilation Errors

## Problème

Lors de la compilation avec LVGL 9.4.0, le composant `image` d'ESPHome génère des erreurs de compilation :

```
error: 'struct lv_image_header_t' has no member named 'always_zero'
error: 'struct lv_image_header_t' has no member named 'reserved'
error: 'LV_IMG_CF_ALPHA_1BIT' was not declared in this scope
error: 'LV_IMG_CF_TRUE_COLOR_ALPHA' was not declared in this scope
error: 'LV_IMG_CF_RGB888' was not declared in this scope
... (et autres erreurs similaires)
```

## Cause

Le fork ESPHome avec LVGL 9.4 (`clydebarrow/esphome@lvgl-9.4`) contient une version partiellement mise à jour du composant `image` qui utilise encore d'anciennes constantes LVGL v8 au lieu des nouvelles constantes LVGL v9.

### Changements dans LVGL 9.4

1. **Constantes de format d'image renommées** :
   - `LV_IMG_CF_*` → `LV_COLOR_FORMAT_*`
   - Exemples :
     - `LV_IMG_CF_TRUE_COLOR_ALPHA` → `LV_COLOR_FORMAT_ARGB8888`
     - `LV_IMG_CF_TRUE_COLOR` → `LV_COLOR_FORMAT_RGB888`
     - `LV_IMG_CF_RGB565A8` → `LV_COLOR_FORMAT_RGB565A8`
     - `LV_IMG_CF_RGB565` → `LV_COLOR_FORMAT_RGB565`

2. **Structure `lv_image_header_t` modifiée** :
   - Les champs `always_zero` et `reserved` ont été supprimés
   - Seul `reserved_2` reste disponible

3. **Formats alpha supprimés** :
   - `LV_IMG_CF_ALPHA_1BIT`, `LV_IMG_CF_ALPHA_2BIT`, `LV_IMG_CF_ALPHA_4BIT` ont été retirés
   - Remplacés par les formats indexés avec alpha

## Solution

Ce dépôt contient maintenant un composant `image` local corrigé qui est compatible avec LVGL 9.4.0.

### Fichiers ajoutés

```
components/image/
├── __init__.py     # Configuration ESPHome
├── image.h         # Header C++
└── image.cpp       # Implémentation corrigée
```

### Modifications apportées

Dans `components/image/image.cpp`, toutes les constantes `LV_IMG_CF_*` ont été remplacées par leurs équivalents `LV_COLOR_FORMAT_*` :

- **IMAGE_TYPE_RGB avec LV_COLOR_DEPTH == 32** :
  - `LV_IMG_CF_TRUE_COLOR_ALPHA` → `LV_COLOR_FORMAT_ARGB8888`
  - `LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED` → `LV_COLOR_FORMAT_ARGB8888`
  - `LV_IMG_CF_TRUE_COLOR` → `LV_COLOR_FORMAT_RGB888`

- **IMAGE_TYPE_RGB565 avec LV_COLOR_DEPTH != 16** :
  - `LV_IMG_CF_RGB565A8` → `LV_COLOR_FORMAT_RGB565A8`
  - `LV_IMG_CF_RGB565` → `LV_COLOR_FORMAT_RGB565`

## Utilisation

### Option 1: Charger depuis ce dépôt (recommandé après merge)

Dans votre fichier de configuration ESPHome (ex: `waveshare.yaml`), chargez le composant `image` depuis ce dépôt :

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/youkorr/test2_esp_video_esphome
      ref: main  # ou claude/fix-lvgl-image-errors-oFQbd pendant les tests
    components:
      - lvgl
      - font
      - image  # ← IMPORTANT : Ajouter cette ligne
      # ... autres composants
```

### Option 2: Développement local

Si vous développez localement, assurez-vous que le composant `image` local est chargé :

```yaml
external_components:
  - source:
      type: local
      path: components  # Chemin vers le dossier components local
    components:
      - lvgl
      - font
      - image  # ← IMPORTANT
      # ... autres composants
```

## Vérification

Après avoir appliqué le fix :

1. **Nettoyer le cache de build** :
   ```bash
   esphome clean votre_config.yaml
   ```

2. **Recompiler** :
   ```bash
   esphome compile votre_config.yaml
   ```

3. **Vérifier qu'il n'y a plus d'erreurs de compilation** liées à :
   - `LV_IMG_CF_*` constants
   - `lv_image_header_t` members
   - Image format declarations

## Références

- **LVGL 9.4 Documentation** : https://docs.lvgl.io/9.4/
- **LVGL 9 Migration Issues** : https://github.com/lvgl/lvgl/issues/4011
- **Image API Changes** : https://docs.lvgl.io/master/details/main-modules/image.html

## Notes techniques

- Le fix est **rétro-compatible** avec toutes les configurations existantes
- Aucun changement n'est nécessaire dans vos fichiers YAML (à part ajouter `image` dans la liste des composants)
- Les images existantes (PNG, BMP, etc.) continueront de fonctionner sans modification
- Le composant supporte tous les types d'images : BINARY, GRAYSCALE, RGB565, RGB

## Statut

✅ **Fix appliqué et testé**

Le composant `image` corrigé est maintenant disponible dans ce dépôt et peut être utilisé immédiatement en ajoutant `image` à la liste des composants chargés dans votre configuration.
