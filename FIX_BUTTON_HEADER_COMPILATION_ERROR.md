# Fix: Button Component Required for ESPHome Core Compatibility

## Problème

Lors de la compilation avec LVGL v9.4 et ESPHome PR #11886, vous rencontrez cette erreur :

```
fatal error: esphome/components/button/button.h: No such file or directory
   39 | #include "esphome/components/button/button.h"
```

Cette erreur apparaît dans :
- `src/esphome/core/application.h:39`
- `src/esphome/core/controller.h:26`

## Cause

Le PR ESPHome #11886 modifie les fichiers core d'ESPHome pour inclure systématiquement les headers du composant `button`. Cependant, ce composant n'est pas présent dans ce dépôt.

## Solution

Un composant `button` stub a été ajouté au dépôt pour satisfaire les exigences de compilation d'ESPHome core.

### Dans votre fichier YAML

**✅ PLUS RIEN À FAIRE !** Les composants `font`, `image`, et `button` sont maintenant chargés **automatiquement** avec LVGL :

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/youkorr/test2_esp_video_esphome
    components:
      - lvgl                 # Charge automatiquement: font, image, button
      # ... autres composants
```

**Ancienne méthode (avant ce fix) :**
```yaml
# ❌ NE PLUS FAIRE - OBSOLÈTE
components:
  - lvgl
  - font     # ❌ Plus besoin de déclarer explicitement
  - image    # ❌ Plus besoin de déclarer explicitement
  - button   # ❌ Plus besoin de déclarer explicitement
```

### Fichiers ajoutés/modifiés

- `components/button/__init__.py` - Enregistrement du composant Python
- `components/button/button.h` - Header C++ minimal
- `components/lvgl/__init__.py` - Ajout de `AUTO_LOAD = ["key_provider", "font", "image", "button"]`

## Comment ça fonctionne

Le composant LVGL utilise maintenant `AUTO_LOAD` pour charger automatiquement ses dépendances obligatoires :

```python
# components/lvgl/__init__.py
DOMAIN = "lvgl"
DEPENDENCIES = ["display"]
AUTO_LOAD = ["key_provider", "font", "image", "button"]  # ✅ Chargement automatique
```

Quand vous déclarez `- lvgl` dans votre YAML, ESPHome charge automatiquement les 4 composants listés dans `AUTO_LOAD`.

## Notes

- Ce composant est un **stub minimal** uniquement pour la compatibilité de compilation
- Il n'ajoute aucune fonctionnalité de bouton physique
- Les boutons LVGL (widgets) fonctionnent normalement via le composant LVGL
- Ce fix est transparent pour vos configurations existantes

## Commit

- Commit: `40af17b`
- Branch: `claude/fix-button-header-BHCUp`
- Date: 2026-01-14
