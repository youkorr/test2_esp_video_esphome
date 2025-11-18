# ⚠️ Configuration LVGL Manquante

## Problème Actuel

Votre fichier `rtsp_ov5647.yaml` est configuré pour **RTSP/Web Server uniquement**.

Pour voir la caméra sur l'écran LCD avec LVGL (et profiter du 50 FPS fluide), vous devez ajouter la configuration de l'écran.

## Ce qui a été ajouté

✅ **Caméra 50 FPS** : `framerate: 50` dans `mipi_dsi_cam`
✅ **LVGL Camera Display** : `update_interval: 20ms` (50 FPS)
✅ **Canvas LVGL** : 800x600 centré sur écran 1024x600

## ❌ Ce qui manque

Votre configuration **display** (écran LCD physique) !

Sans display configuré, vous aurez l'erreur :
```
Error: Display 'main_display' not found
```

## Solution 1: Configuration Display Complète (Recommandée)

Si vous avez un écran LCD 1024x600 sur votre ESP32-P4, ajoutez cette configuration **AVANT** la section `lvgl:` dans `rtsp_ov5647.yaml` :

```yaml
# Display configuration (ESP32-P4 LCD 1024x600)
display:
  - platform: rgb_display
    id: main_display
    width: 1024
    height: 600
    data_pins:
      red:
        - GPIO8    # R0
        - GPIO9    # R1
        - GPIO10   # R2
        - GPIO11   # R3
        - GPIO12   # R4
      green:
        - GPIO45   # G0
        - GPIO46   # G1
        - GPIO47   # G2
        - GPIO48   # G3
        - GPIO0    # G4
        - GPIO21   # G5
      blue:
        - GPIO40   # B0
        - GPIO41   # B1
        - GPIO42   # B2
        - GPIO2    # B3
        - GPIO1    # B4
    hsync_pin: GPIO3
    vsync_pin: GPIO17
    pclk_pin: GPIO9
    de_pin: GPIO18
    rotation: 0
    auto_clear_enabled: false
```

**⚠️ IMPORTANT** : Les GPIO ci-dessus sont des exemples. Vérifiez votre schéma de board pour les GPIO exacts !

### Trouver vos GPIO d'écran

Votre ESP32-P4-Function-EV-Board a probablement une documentation avec le schéma. Cherchez :
- **RGB LCD pins** : RED[0-4], GREEN[0-5], BLUE[0-4]
- **Control pins** : HSYNC, VSYNC, PCLK, DE (data enable)
- **Backlight pin** : LEDC_BL

## Solution 2: Utiliser un Fichier LVGL Existant

Si vous avez déjà une configuration LVGL complète, incluez-la :

```yaml
# Dans rtsp_ov5647.yaml, en haut du fichier
substitutions:
  device_name: "esp32-p4-camera"
  friendly_name: "ESP32-P4 RTSP Camera"

# Inclure la configuration LVGL complète
<<: !include LVGL_CAMERA_PAGE_OV5647_800x600.yaml

# Le reste de votre config...
esphome:
  name: ${device_name}
  ...
```

## Solution 3: Mode RTSP Seulement (Sans écran LCD)

Si vous n'avez **pas d'écran LCD** et voulez juste RTSP/web server :

**Supprimez ces sections dans `rtsp_ov5647.yaml` :**
```yaml
# Supprimez ou commentez :
# lvgl:
#   ...

# lvgl_camera_display:
#   ...
```

Gardez seulement:
- `mipi_dsi_cam` (caméra 50 FPS)
- `rtsp_server` (stream RTSP)
- `camera_web_server` (stream MJPEG)

## Vérification Rapide

### Vous avez un écran LCD ?

**OUI** → Utilisez Solution 1 ou 2
**NON** → Utilisez Solution 3 (mode RTSP seulement)

### Comment savoir si vous avez un écran ?

1. Regardez votre board physiquement - y a-t-il un écran LCD attaché ?
2. Vérifiez le nom de votre board :
   - **ESP32-P4-Function-EV-Board** = A généralement un écran 1024x600
   - **ESP32-P4-Dev-Kit** = Pas d'écran par défaut

## Test Après Configuration

### Si vous avez ajouté display + LVGL :

1. **Compilez** votre configuration
2. **Flashez** votre ESP32-P4
3. **Vérifiez les logs** pour :
```
[I][lvgl_camera_display] Canvas: 800x600 @ (112, 0)
[I][lvgl_camera_display] Update interval: 20ms (50 FPS)
[I][mipi_dsi_cam] Using CUSTOM format: 800x600 RAW8 @ 50fps
```

4. **Sur l'écran LCD** : Vous devriez voir la vidéo fluide à 50 FPS

### Si vous utilisez mode RTSP seulement :

1. **Compilez** et **flashez**
2. **Connectez** via RTSP : `rtsp://youkorr:youkorr123@<IP>:554/stream`
3. **Vérifiez** que le stream est à 50 FPS dans VLC/Frigate

## Fichiers de Référence

### Configuration LVGL Complète
Voir `LVGL_CAMERA_PAGE_OV5647_800x600.yaml` pour un exemple complet avec :
- Canvas camera 800x600
- Boutons START/STOP
- Mode plein écran
- Overlay d'infos

### Documentation
- `OV5647_SMOOTH_MOTION_FIX.md` : Explication du passage à 50 FPS
- `OV5647_IMAGE_QUALITY_GUIDE.md` : Optimisation qualité d'image
- `WATCHDOG_TIMEOUT_LVGL_FIX.md` : Problèmes watchdog et update_interval

## Questions Fréquentes

### Q: J'ai l'erreur "Display 'main_display' not found"
**R:** Vous n'avez pas configuré le display. Ajoutez la section `display:` (Solution 1).

### Q: L'écran est noir
**R:** Vérifiez :
1. GPIO du display corrects
2. Backlight activé
3. Canvas créé : `camera_canvas` existe dans LVGL

### Q: L'image est toujours saccadée
**R:** Vérifiez :
1. `framerate: 50` dans `mipi_dsi_cam` ✅
2. `update_interval: 20ms` dans `lvgl_camera_display` ✅
3. Pas de watchdog timeout (essayez 33ms si problème)

### Q: Watchdog timeout avec update_interval: 20ms
**R:** C'est normal sur certains systèmes. Utilisez :
```yaml
lvgl_camera_display:
  update_interval: 33ms  # 30 FPS - toujours plus fluide que 10 FPS
```

### Q: Je veux juste RTSP, pas d'écran
**R:** Supprimez `lvgl:` et `lvgl_camera_display:` de votre config. Le 50 FPS fonctionnera quand même pour RTSP.

## Résumé

**Pour mouvements fluides LVGL :**
1. ✅ Caméra 50 FPS (déjà configuré)
2. ✅ lvgl_camera_display 20ms/33ms (déjà configuré)
3. ❌ **Display LCD** (À AJOUTER - voir Solution 1)
4. ✅ Canvas LVGL (déjà configuré)

**Sans ces 4 éléments, LVGL ne fonctionnera pas !**

---

**Besoin d'aide ?**
1. Vérifiez le schéma de votre board pour les GPIO LCD
2. Consultez `LVGL_CAMERA_PAGE_OV5647_800x600.yaml` pour un exemple complet
3. Ou désactivez LVGL et utilisez seulement RTSP (Solution 3)
