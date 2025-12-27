# 🚀 Migration rapide LVGL V8 → V9 pour Lottie

## Étape 1 : Ajouter external_components LVGL V9

Dans votre fichier YAML principal, **remplacer** la section external_components :

```yaml
external_components:
  # 🆕 LVGL V9.4 au lieu de V8
  - source:
      type: git
      url: https://github.com/clydebarrow/esphome
      ref: lvgl-9.4
    components:
      - lvgl
      - font
      - image
    refresh: 1d

  # Vos composants locaux (inchangés)
  - source:
      type: local
      path: components
    components:
      - storage
      - lvgl_advanced_features  # 🆕 NOUVEAU
      - esp_cam_sensor
      - lvgl_camera_display
      # ... tous vos autres composants
```

## Étape 2 : Activer ThorVG/Lottie

Ajouter cette section **avant** la configuration `lvgl:` :

```yaml
# 🆕 Activer ThorVG et Lottie
lvgl_advanced_features:
  thorvg:
    internal: true
  svg: true
  lottie: true
  gif: true

  # Performance
  draw_sw_complex: true
  shadow_cache_size: 16
  img_cache_size: 8
```

## Étape 3 : Utiliser Lottie dans votre page

Maintenant `lottie:` sera reconnu :

```yaml
pages:
  - id: page_home
    widgets:
      # ✅ Lottie fonctionne avec V9
      - lottie:
          id: page_transition_spinner
          src: "/sdcard/LottieFiles/Sandy Loading.json"
          width: 100
          height: 100
          x: 590
          y: 310
          loop: true
          autoplay: false
```

## Étape 4 : Compiler

```bash
# Nettoyer le cache
esphome clean votre_config.yaml

# Compiler avec V9
esphome compile votre_config.yaml
```

## Vérification

Dans les logs de compilation, vous devez voir :
```
Library Manager: Installing lvgl/lvgl @ 9.4.0  ✅
[lvgl_advanced_features] ThorVG Internal: ENABLED  ✅
[lvgl_advanced_features] Lottie Support: ENABLED  ✅
```

## Si erreurs de compilation

Voir `MIGRATION_LVGL_V9_README.md` section "Résolution de problèmes"
