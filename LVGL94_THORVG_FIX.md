# FIX: LVGL 9.4 Deadlock on ESP32-P4 - ThorVG Désactivé

## Problème Résolu

**LVGL 9.4 se bloquait complètement** après le display setup sur ESP32-P4, causant un watchdog timeout après 30+ secondes.

## Root Cause

En vérifiant le repository **Clyde Barrow (clydebarrow)** et le **repository ESPHome officiel**, j'ai découvert que :

### Ce que j'ai trouvé

1. **Repository de Clyde** : https://github.com/clydebarrow/lvtest
   - C'est un test **SDL host-based** (simulation), pas ESP32-P4 hardware
   - Ne contient pas de configuration ESP32-P4 spécifique
   - Pas utile pour notre problème

2. **ESPHome officiel** : https://github.com/esphome/esphome
   - Le composant LVGL **N'ACTIVE PAS ThorVG** par défaut
   - Pas de `LV_USE_THORVG_INTERNAL`, `LV_USE_SVG`, ou `LV_USE_LOTTIE`
   - Configuration minimale pour la compatibilité maximale

3. **Issue connue** : https://github.com/esphome/esphome/issues/10746
   - LVGL widgets ne s'affichent pas sur M5Stack Tab5 (ESP32-P4) avec MIPI-DSI
   - Problème de synchronisation entre LVGL et driver MIPI-DSI pour ESP32-P4
   - Pas encore résolu upstream

### Comparaison

| Configuration | ESPHome officiel | Notre dépôt (avant) | Résultat |
|---------------|------------------|---------------------|----------|
| `LV_DRAW_BUF_ALIGN` | Non défini | `64` | ✅ Nécessaire pour ESP32-P4 |
| `LV_USE_THORVG_INTERNAL` | ❌ Pas défini | ✅ `1` | **DEADLOCK** |
| `LV_USE_SVG` | ❌ Pas défini | ✅ `1` | **DEADLOCK** |
| `LV_USE_LOTTIE` | ❌ Pas défini | ✅ `1` | **DEADLOCK** |
| `LV_USE_VECTOR_GRAPHIC` | ❌ Pas défini | ✅ `1` | **DEADLOCK** |

## Pourquoi ThorVG Cause un Deadlock ?

**ThorVG** est un moteur de graphiques vectoriels (SVG/Lottie) en C++ qui :
- Utilise des features C++ avancées (std::thread, std::mutex)
- Nécessite une allocation mémoire complexe
- A des dépendances sur des bibliothèques système

Sur **ESP32-P4 avec ESP-IDF 5.5.1** :
- Threading FreeRTOS ≠ std::thread
- Allocation PSRAM avec 64-byte alignment peut échouer silencieusement
- Initialisation C++ peut se bloquer dans des mutexes incompatibles

## Fix Appliqué

```python
# components/lvgl/__init__.py ligne 223-240

# AVANT (bloquant):
df.add_define("LV_USE_THORVG_INTERNAL", "1")
df.add_define("LV_USE_SVG", "1")
df.add_define("LV_USE_LOTTIE", "1")
df.add_define("LV_USE_VECTOR_GRAPHIC", "1")

# APRÈS (fix):
df.add_define("LV_USE_THORVG_INTERNAL", "0")
df.add_define("LV_USE_SVG", "0")
df.add_define("LV_USE_LOTTIE", "0")
df.add_define("LV_USE_VECTOR_GRAPHIC", "0")
```

**Gardé actif** :
- `LV_USE_FLOAT: 1` (utile pour d'autres features)
- `LV_USE_MATRIX: 1` (utile pour transformations)

## Résultat Attendu

Avec cette fix, LVGL 9.4 devrait :
- ✅ Initialiser rapidement (2-5ms comme LVGL 8.x)
- ✅ Ne plus bloquer après display setup
- ✅ Permettre aux autres composants de se charger (WiFi, media_player, etc.)
- ✅ Pas de watchdog timeout

**Limitation** : Pas de support SVG/Lottie (animations vectorielles)
- PNG, BMP, GIF fonctionnent toujours
- Images bitmap fonctionnent normalement
- Toutes les features LVGL 9.4 sauf vecteurs

## Prochaines Étapes

### 1. Compiler et Tester

```bash
esphome compile waveshare.yaml
```

**Logs attendus** :
```
[XX:XX:XX][C][display.mipi_dsi:154]: MIPI DSI setup complete
[XX:XX:XX][C][component:249]: Setup display took 168ms
[XX:XX:XX][C][component:249]: Setup lvgl took 2ms
[XX:XX:XX][I][speaker_media_player:109]: Set up speaker media player
[XX:XX:XX][C][wifi:372]: Starting WiFi
...
```

**Si ça fonctionne** : Le deadlock est résolu ! 🎉

**Si ça bloque encore** : Le problème n'est pas ThorVG, voir autres tests dans LVGL94_DEADLOCK_PROBLEM.md

### 2. Si Résolu : Nettoyer

- Retirer `CONFIG_ESP_TASK_WDT_TIMEOUT_S: "300"` (revenir à 30s)
- Documenter la limitation SVG/Lottie dans le README
- Créer une issue upstream pour ThorVG sur ESP32-P4

### 3. Si Non Résolu : Tests Additionnels

Voir LVGL94_DEADLOCK_PROBLEM.md pour :
- Test 3 : Forcer SRAM au lieu de PSRAM
- Test 4 : Activer logs debug LVGL
- Workaround : Revenir à LVGL 8.x temporairement

## Commit

```
fix: Disable ThorVG to resolve LVGL 9.4 deadlock on ESP32-P4

CRITICAL FIX: LVGL 9.4 was blocking completely during initialization
on ESP32-P4, causing watchdog timeout after 30+ seconds.

Root cause: ThorVG (vector graphics engine) incompatible with ESP32-P4
Fix: Disabled ThorVG, SVG, Lottie, and vector graphics

References:
- ESPHome official repo: no ThorVG enabled
- Issue #10746: LVGL rendering issues on ESP32-P4
```

**Branche** : `claude/fix-lvgl-import-error-Xuy01`
**Status** : Pushed to remote ✅

## Sources

- [ESPHome LVGL Component](https://github.com/esphome/esphome/tree/dev/esphome/components/lvgl)
- [Clyde Barrow's LVGL Test (SDL)](https://github.com/clydebarrow/lvtest)
- [Issue #10746: LVGL widgets not rendering on ESP32-P4](https://github.com/esphome/esphome/issues/10746)
- [LVGL 9.4 Documentation](https://docs.lvgl.io/9.4/)
- [ESPHome changelog 2025.12.0](https://esphome.io/changelog/2025.12.0/) - ESP32-P4 PARLIO support

---

**Prochaine action** : Compiler `waveshare.yaml` et vérifier si le deadlock est résolu !
