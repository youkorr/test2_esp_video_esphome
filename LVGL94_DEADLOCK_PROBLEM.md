# PROBLÈME CRITIQUE : LVGL 9.4 Se Bloque Complètement sur ESP32-P4

## 🐛 Symptômes

**LVGL 9.4** : Blocage complet après display setup
```
[02:28:46][C][display.mipi_dsi:154]: MIPI DSI setup complete
[02:28:46][C][component:249]: Setup display took 168ms
[02:29:16]E (34084) task_wdt: Task watchdog got triggered (30s timeout)
```
→ AUCUN autre composant ne se charge après le display
→ Watchdog timeout après 30 secondes

**Sans LVGL 9.4** (LVGL 8.x) : Fonctionne parfaitement
```
[02:32:21][C][display.mipi_dsi:141]: MIPI DSI setup complete
[02:32:21][C][component:208]: Setup display took 168ms
[02:32:21][C][component:208]: Setup interval took 0ms
[02:32:21][C][component:208]: Setup lvgl took 2ms
[02:32:21][I][speaker_media_player:109]: Set up speaker media player
[02:32:23][C][wifi:372]: Starting WiFi
... (tout fonctionne normalement)
```

## 🔍 Analyse

### Différence Clé

**LVGL 8.x** :
- Display setup : 168ms ✅
- LVGL setup : 2ms ✅
- Tous les composants se chargent ✅

**LVGL 9.4** :
- Display setup : 168ms ✅
- LVGL setup : **BLOQUÉ** ❌
- Aucun autre composant ne se charge ❌
- Watchdog timeout après 30s ❌

### Cause Probable

LVGL 9.4 se **bloque complètement** (deadlock ou boucle infinie) dans son initialisation sur ESP32-P4.

Possibles causes :
1. **Alignement mémoire 64 bytes** : L'allocation de buffers alignés bloque ou échoue silencieusement
2. **PSRAM** : LVGL 9.4 essaie d'allouer dans PSRAM et se bloque
3. **ThorVG** : L'initialisation de ThorVG (SVG/Lottie) se bloque
4. **Threading** : Un problème de mutex/lock dans LVGL 9.4

## ✅ Fixes Déjà Appliqués

### Fix 1 : Alignement Mémoire (commit 726735b)
```python
LV_DRAW_BUF_ALIGN = 64  # ESP32-P4 nécessite 64 bytes
```
→ **Résolu le crash "Load access fault"** ✅
→ **N'a PAS résolu le blocage** ❌

### Fix 2 : Watchdog Timeout (commit 32d73c2)
```yaml
CONFIG_ESP_TASK_WDT_TIMEOUT_S: "30"
```
→ **Augmente le timeout de 5s à 30s** ✅
→ **N'a PAS résolu le blocage** (timeout quand même) ❌

## 🧪 Tests à Faire

### Test 1 : Désactiver complètement le watchdog ❌
```yaml
esp32:
  framework:
    sdkconfig_options:
      CONFIG_ESP_TASK_WDT_EN: "n"  # Désactive watchdog
```
**But** : Voir si ça finit par passer après plusieurs minutes (exclure timeout simple)
**Résultat** : ❌ ERREUR DE COMPILATION - ESPHome OTA backend nécessite `CONFIG_ESP_TASK_WDT_TIMEOUT_S`

### Test 2 : Configuration LVGL minimale ✅ EN COURS
```python
# Dans components/lvgl/__init__.py
# Désactiver ThorVG temporairement
df.add_define("LV_USE_THORVG_INTERNAL", "0")  # Au lieu de 1
df.add_define("LV_USE_SVG", "0")
df.add_define("LV_USE_LOTTIE", "0")
df.add_define("LV_USE_VECTOR_GRAPHIC", "0")
```
**But** : Identifier si ThorVG cause le blocage
**Résultat** : ✅ FIX APPLIQUÉ - Voir commit suivant

### Test 3 : Allocation mémoire LVGL
```python
# Forcer LVGL à utiliser SRAM au lieu de PSRAM
df.add_define("LV_USE_STDLIB_MALLOC", "LV_STDLIB_BUILTIN")
# ou
df.add_define("LV_MEM_CUSTOM", "0")
```
**But** : Voir si le problème vient de PSRAM

### Test 4 : Logs debug LVGL
```python
# Activer les logs debug LVGL
df.add_define("LV_USE_LOG", "1")
df.add_define("LV_LOG_LEVEL", "LV_LOG_LEVEL_TRACE")
```
**But** : Voir où exactement LVGL se bloque

## 🔄 Workaround Temporaire

**Revenir à LVGL 8.x** jusqu'à ce que le problème soit résolu :

```python
# Dans components/lvgl/__init__.py
cg.add_library("lvgl/lvgl", "8.4.0")  # Au lieu de 9.4.0
```

## 📊 Configuration Actuelle

**Système** :
- ESP32-P4 @ 360MHz
- 32MB Flash
- ~8MB PSRAM
- ESP-IDF 5.5.1

**LVGL 9.4 Config** :
- LV_DRAW_BUF_ALIGN: 64 bytes
- LV_USE_THORVG_INTERNAL: 1 (ThorVG activé)
- LV_USE_SVG: 1
- LV_USE_LOTTIE: 1
- LV_USE_STDLIB_MALLOC: LV_STDLIB_CUSTOM

**Display** :
- MIPI DSI
- Setup time: 168ms (identique avec/sans LVGL 9.4)

## 🎯 Prochaines Étapes

1. ✅ Désactiver watchdog pour test → ❌ Erreur de compilation
2. ✅ Tester sans ThorVG → ✅ FIX APPLIQUÉ
3. ⏳ Compiler et tester si le deadlock est résolu
4. ⏳ Si résolu : documenter et commit
5. ⏳ Si non résolu : tester sans PSRAM ou activer logs debug

## 🔍 ROOT CAUSE IDENTIFIÉE

**Comparaison avec ESPHome officiel** :

| Configuration | ESPHome officiel | Notre dépôt | Résultat |
|---------------|------------------|-------------|----------|
| `LV_DRAW_BUF_ALIGN` | Non défini | `64` | ✅ Nécessaire pour ESP32-P4 |
| `LV_USE_THORVG_INTERNAL` | Non défini | `1` | ❌ CAUSE LE DEADLOCK |
| `LV_USE_SVG` | Non défini | `1` | ❌ CAUSE LE DEADLOCK |
| `LV_USE_LOTTIE` | Non défini | `1` | ❌ CAUSE LE DEADLOCK |
| `LV_USE_VECTOR_GRAPHIC` | Non défini | `1` | ❌ CAUSE LE DEADLOCK |

**Découverte** : En vérifiant le repository ESPHome officiel, **ThorVG n'est PAS activé** dans leur configuration LVGL. C'est une fonctionnalité qui a été ajoutée manuellement dans notre dépôt.

**Hypothèse** : ThorVG (moteur de graphiques vectoriels) se bloque lors de son initialisation sur ESP32-P4, probablement à cause de :
- Threading/mutex incompatibles avec ESP32-P4
- Allocation mémoire C++ qui échoue silencieusement
- Initialisation de bibliothèques C++ qui ne fonctionnent pas avec ESP-IDF 5.5.1

**Fix appliqué** : Désactivation de ThorVG et tous les features de graphiques vectoriels

## 📝 Notes

- Le crash "Load access fault" est **RÉSOLU** (alignement 64 bytes)
- Le **blocage complet** est un problème **DIFFÉRENT**
- LVGL 8.x fonctionne **parfaitement** sur la même configuration
- Le problème est **spécifique à LVGL 9.4** sur ESP32-P4

---

**Status** : 🔴 BLOQUANT
**Priorité** : CRITIQUE
**Impact** : Impossible d'utiliser LVGL 9.4 sur ESP32-P4
