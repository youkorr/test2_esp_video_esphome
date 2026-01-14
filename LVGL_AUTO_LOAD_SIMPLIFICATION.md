# Simplification: Auto-chargement des dépendances LVGL

## Résumé

Les composants `font`, `image`, et `button` sont maintenant **chargés automatiquement** avec LVGL. Vous n'avez plus besoin de les déclarer explicitement dans votre configuration YAML.

## Configuration simplifiée

### ✅ Nouvelle méthode (recommandée)

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/youkorr/test2_esp_video_esphome
    components:
      - lvgl        # Charge automatiquement: font, image, button, key_provider
      - storage     # Vos autres composants...
```

### ❌ Ancienne méthode (obsolète)

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/youkorr/test2_esp_video_esphome
    components:
      - lvgl
      - font        # ❌ Plus nécessaire
      - image       # ❌ Plus nécessaire
      - button      # ❌ Plus nécessaire
```

## Pourquoi ce changement ?

### Avant
Les utilisateurs devaient se souvenir de déclarer 3-4 composants obligatoires :
- `lvgl` - Le composant principal
- `font` - Pour les polices LVGL 9.x
- `image` - Pour les images LVGL 9.x
- `button` - Stub pour compatibilité ESPHome core

**Problème :** Facile d'oublier un composant et obtenir des erreurs de compilation cryptiques.

### Après
Il suffit de déclarer `lvgl` et les dépendances obligatoires sont chargées automatiquement via `AUTO_LOAD`.

**Avantages :**
- ✅ Configuration plus simple
- ✅ Moins d'erreurs
- ✅ Meilleure expérience utilisateur
- ✅ Aligné avec les conventions ESPHome

## Détails techniques

### Modification dans `components/lvgl/__init__.py`

```python
DOMAIN = "lvgl"
DEPENDENCIES = ["display"]
AUTO_LOAD = ["key_provider", "font", "image", "button"]  # ✅ Ajout de font, image, button
```

### Composants auto-chargés

| Composant | Rôle | Raison |
|-----------|------|--------|
| `key_provider` | Gestion des entrées clavier | Déjà présent avant ce changement |
| `font` | Support des polices LVGL 9.x | Obligatoire pour LVGL |
| `image` | Support des images LVGL 9.x | Obligatoire pour LVGL |
| `button` | Stub pour ESPHome core | Requis par application.h et controller.h |

## Migration

### Si vous utilisez déjà ce dépôt

1. Mettez à jour vers la dernière version :
   ```bash
   git pull origin main
   ```

2. Supprimez les déclarations explicites de `font`, `image`, `button` dans votre YAML

3. Recompilez :
   ```bash
   esphome run votre_config.yaml
   ```

### Compatibilité

✅ **Rétro-compatible** : Si vous gardez les déclarations explicites, ça fonctionne toujours (ESPHome ignore les doublons).

## Exemple complet

```yaml
esphome:
  name: mon-esp32-p4
  platform: esp32
  board: esp32-p4-function-ev-board

external_components:
  - source:
      type: git
      url: https://github.com/youkorr/test2_esp_video_esphome
    components:
      - lvgl                    # ✅ Charge automatiquement font, image, button
      - storage                 # Pour SD card + ThorVG
      - esp_cam_sensor          # Si vous utilisez une caméra
      - lvgl_camera_display     # Pour afficher la caméra dans LVGL

# ... reste de votre config
```

## Notes

- Cette simplification est **transparente** pour les configurations existantes
- Les composants `font`, `image`, et `button` restent disponibles si vous voulez les déclarer explicitement
- Le composant `button` est un stub minimal pour la compatibilité ESPHome core (voir `FIX_BUTTON_HEADER_COMPILATION_ERROR.md`)

## Références

- PR #96 : Ajout du composant `button` stub
- Cette PR : Simplification via `AUTO_LOAD`
- Documentation LVGL v9 : `MIGRATION_LVGL_V9_README.md`
