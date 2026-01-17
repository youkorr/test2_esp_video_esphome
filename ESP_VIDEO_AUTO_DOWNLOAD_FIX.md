# Correction du Système Auto-Download ESP-Video

## 🐛 Problème Détecté

Lors de la première compilation, le système d'auto-download essayait de télécharger tous les composants même s'ils existaient déjà localement :

```
INFO 📦 esp_h264: Encodeur/décodeur H.264
INFO    ✓ Already downloaded (cached)  ✅ OK
INFO 📦 esp_cam_sensor: Drivers caméra
INFO 📥 Downloading esp_cam_sensor...
ERROR    ✗ Component path not found in repo: esp_cam_sensor  ❌ ERREUR
```

## 🔍 Cause du Problème

**2 problèmes identifiés :**

### 1. Détection des composants locaux défaillante

```python
# AVANT (bugué)
def is_component_downloaded(dep, target_dir):
    if not os.path.exists(target_dir):
        return False
    if not os.listdir(target_dir):
        return False

    # Vérifier le state file
    state = load_download_state()
    if component_name in state:
        if state[component_name].get('hash') == dep_hash:
            return True

    return False  # ❌ BUG: Retourne False même si le composant existe !
```

**Résultat** : Les composants locaux sans fichier de state n'étaient pas détectés.

### 2. Chemins incorrects dans ESP_VIDEO_DEPENDENCIES

```python
# AVANT (incorrect)
{
    "name": "esp_cam_sensor",
    "repo": "https://github.com/espressif/esp-adf-libs.git",  # ❌ FAUX
    "sparse_paths": ["esp_cam_sensor"],  # Ce composant n'existe PAS dans ce repo!
}
```

**Réalité** :
- ✅ **esp_h264** est dans esp-adf-libs
- ❌ **esp_cam_sensor**, **esp_ipa**, **esp_sccb_intf** sont dans le Component Registry ESP-IDF (pas dans esp-adf-libs)

## ✅ Solutions Appliquées

### Fix 1: Détection des composants locaux corrigée

```python
# APRÈS (corrigé)
def is_component_downloaded(dep, target_dir):
    if not os.path.exists(target_dir):
        return False

    # Vérifier si le répertoire contient des fichiers
    try:
        dir_contents = os.listdir(target_dir)
        if not dir_contents:
            return False
    except Exception:
        return False

    # ✅ Si le répertoire existe et contient des fichiers, c'est OK
    # (même si le state file n'existe pas - compatibilité avec composants existants)
    _LOGGER.debug(f"Component {dep['name']} found locally at {target_dir}")

    # State file optionnel (pour versioning)
    state = load_download_state()
    # ... vérifications optionnelles ...

    # ✅ Retourner True si le composant existe localement
    return True  # ✅ FIX: Retourne True si le composant existe
```

**Résultat** : Les composants locaux sont maintenant détectés correctement, avec ou sans fichier de state.

### Fix 2: Configuration des dépendances mise à jour

```python
# APRÈS (corrigé)
ESP_VIDEO_DEPENDENCIES = [
    {
        "name": "esp_h264",
        "repo": "https://github.com/espressif/esp-adf-libs.git",  # ✅ Peut être téléchargé
        "tag": "master",
        "sparse_paths": ["esp_h264"],
        "description": "Encodeur/décodeur H.264 (OpenH264 + TinyH264)",
        "required": True
    },
    {
        "name": "esp_cam_sensor",
        "repo": None,  # ✅ Doit être présent localement (Component Registry)
        "tag": None,
        "sparse_paths": [],
        "description": "Drivers caméra (OV5647, SC202CS, OV02C10)",
        "required": True
    },
    {
        "name": "esp_ipa",
        "repo": None,  # ✅ Doit être présent localement
        "tag": None,
        "sparse_paths": [],
        "description": "Image Processing Algorithms (AWB, denoise, sharpen)",
        "required": True
    },
    {
        "name": "esp_sccb_intf",
        "repo": None,  # ✅ Doit être présent localement
        "tag": None,
        "sparse_paths": [],
        "description": "Interface I2C/SCCB pour caméras",
        "required": True
    },
]
```

### Fix 3: Gestion améliorée des composants sans repo

```python
# Dans ensure_esp_video_dependencies()
for dep in ESP_VIDEO_DEPENDENCIES:
    component_name = dep['name']
    target_dir = os.path.join(components_dir, component_name)
    has_repo = dep['repo'] is not None

    # Vérifier si déjà présent localement
    if is_component_downloaded(dep, target_dir):
        _LOGGER.info(f"   ✓ Found locally")  # ✅ Détection locale
        local_count += 1
        continue

    # Si pas de repo, on ne peut pas télécharger
    if not has_repo:
        _LOGGER.warning(f"   ⚠️ Not found locally and no download source available")
        if dep.get('required', True):
            missing_components.append(component_name)
            all_ok = False
        continue

    # Télécharger seulement si repo disponible
    # ...
```

## 🧪 Test de Validation

```bash
$ python3 -c "from esp_video_download import ensure_esp_video_dependencies; ..."
```

**Résultat** :
```
============================================================
ESP-Video Auto-Download (like LVGL 9.4)
============================================================
📦 esp_h264: Encodeur/décodeur H.264 (OpenH264 + TinyH264)
   ✓ Found locally  ✅
📦 esp_cam_sensor: Drivers caméra (OV5647, SC202CS, OV02C10)
   ✓ Found locally  ✅
📦 esp_ipa: Image Processing Algorithms (AWB, denoise, sharpen)
   ✓ Found locally  ✅
📦 esp_sccb_intf: Interface I2C/SCCB pour caméras
   ✓ Found locally  ✅
============================================================
📦 Found 4 local component(s)
✅ All ESP-Video dependencies ready!
============================================================
Result: True  ✅
```

## 📊 Comparaison Avant/Après

| Composant | Avant | Après |
|-----------|-------|-------|
| **esp_h264** | ✅ Détecté | ✅ Détecté |
| **esp_cam_sensor** | ❌ Erreur de téléchargement | ✅ Détecté localement |
| **esp_ipa** | ❌ Erreur de téléchargement | ✅ Détecté localement |
| **esp_sccb_intf** | ❌ Erreur de téléchargement | ✅ Détecté localement |
| **Compilation** | ❌ Échoue | ✅ Devrait réussir |

## 🎯 Comportement Final

### Scénario 1: Dépôt avec composants existants (votre cas)

```yaml
components/
├── esp_h264/          ✅ Détecté localement
├── esp_cam_sensor/    ✅ Détecté localement
├── esp_ipa/           ✅ Détecté localement
└── esp_sccb_intf/     ✅ Détecté localement

Résultat: ✅ Tous détectés, aucun téléchargement, compilation OK
```

### Scénario 2: Nouveau projet sans composants

```yaml
components/
└── esp_video/  (seulement)

Résultat:
- esp_h264: 📥 Téléchargé automatiquement depuis esp-adf-libs
- esp_cam_sensor, esp_ipa, esp_sccb_intf: ⚠️ Doivent être ajoutés manuellement
  (Component Registry ESP-IDF ou autre source)
```

### Scénario 3: esp_h264 manquant, autres présents

```yaml
components/
├── esp_cam_sensor/    ✅ Détecté localement
├── esp_ipa/           ✅ Détecté localement
└── esp_sccb_intf/     ✅ Détecté localement
# esp_h264 manquant

Résultat:
- esp_h264: 📥 Téléchargé automatiquement depuis esp-adf-libs
- Autres: ✅ Utilisés localement
```

## 📝 Notes Importantes

1. **Backward Compatibility** : Les composants existants sont détectés même sans fichier de state

2. **Composants ESP-IDF** : `esp_cam_sensor`, `esp_ipa`, `esp_sccb_intf` ne peuvent pas être auto-téléchargés car ils sont sur le Component Registry ESP-IDF (différent d'esp-adf-libs)

3. **esp_h264** : Peut être auto-téléchargé depuis esp-adf-libs si manquant

4. **Future Enhancement** : Possibilité d'ajouter le téléchargement via ESP-IDF Component Manager pour les autres composants

## 🚀 Prochaine Étape

**Relancez la compilation** :
```bash
esphome compile your-config.yaml
```

Vous devriez maintenant voir :
```
INFO ✅ All ESP-Video dependencies ready!
INFO Compiling app...
```

## 📦 Commits

1. **f97c23d** - "feat: Add auto-download system for ESP-Video dependencies (like LVGL 9.4)"
2. **29441e5** - "fix: Detect existing local components correctly in auto-download system" ✅ ACTUEL

---

**Statut** : ✅ Corrigé et testé
**Branche** : `claude/fix-lvgl-import-error-Xuy01`
**Prêt pour** : Compilation
