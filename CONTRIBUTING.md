# 🤝 Guide de Contribution

Merci de votre intérêt pour contribuer à ce projet ! Ce guide vous aidera à démarrer.

---

## 📋 Table des Matières

1. [Code de Conduite](#code-de-conduite)
2. [Comment Contribuer](#comment-contribuer)
3. [Types de Contributions](#types-de-contributions)
4. [Processus de Développement](#processus-de-développement)
5. [Standards de Code](#standards-de-code)
6. [Tests](#tests)
7. [Documentation](#documentation)
8. [Licence](#licence)

---

## 🌟 Code de Conduite

Ce projet adhère aux principes de respect et de collaboration. Nous attendons de tous les contributeurs :

- **Respect mutuel** : Soyez respectueux envers les autres contributeurs
- **Constructivité** : Les critiques doivent être constructives
- **Inclusion** : Accueillir les nouveaux contributeurs
- **Professionnalisme** : Maintenir un environnement professionnel

---

## 🚀 Comment Contribuer

### 1. Fork et Clone

```bash
# Fork le dépôt sur GitHub (bouton "Fork")

# Clone votre fork
git clone https://github.com/VOTRE_NOM/test2_esp_video_esphome.git
cd test2_esp_video_esphome

# Ajouter le dépôt original comme remote
git remote add upstream https://github.com/youkorr/test2_esp_video_esphome.git
```

### 2. Créer une Branche

```bash
# Mettre à jour depuis upstream
git fetch upstream
git checkout main
git merge upstream/main

# Créer une nouvelle branche
git checkout -b feature/ma-fonctionnalite
# ou
git checkout -b fix/mon-correctif
```

### 3. Faire vos Changements

Développez votre fonctionnalité ou correctif en suivant les [Standards de Code](#standards-de-code).

### 4. Commit

```bash
# Ajouter vos fichiers
git add .

# Commit avec message descriptif
git commit -m "Add: Support pour nouveau capteur OV3660"
```

**Format des messages de commit** :

```
Type: Description courte (50 caractères max)

Description détaillée si nécessaire (72 caractères par ligne).

Fixes #123
```

**Types de commit** :
- `Add:` - Nouvelle fonctionnalité
- `Fix:` - Correction de bug
- `Docs:` - Modification documentation
- `Refactor:` - Refactorisation code
- `Test:` - Ajout/modification tests
- `Perf:` - Amélioration performance
- `Style:` - Formatage code

### 5. Push et Pull Request

```bash
# Push vers votre fork
git push origin feature/ma-fonctionnalite
```

Puis sur GitHub :
1. Ouvrir une **Pull Request** depuis votre branche vers `main`
2. Remplir le template de PR
3. Attendre la review

---

## 💡 Types de Contributions

### 🐛 Rapporter des Bugs

Ouvrir une **Issue** avec :
- **Titre clair** : "Bug: Caméra ne démarre pas avec OV5647"
- **Description** : Ce qui se passe vs ce qui devrait se passer
- **Environnement** :
  ```
  ESPHome version: 2024.x.x
  Board: ESP32-P4-Function-EV-Board
  Composant: esp_cam_sensor
  ```
- **Logs** : Copier les logs pertinents
- **Configuration** : Votre fichier YAML (masquer les secrets)

### ✨ Proposer des Fonctionnalités

Ouvrir une **Issue** "Feature Request" avec :
- **Cas d'usage** : Pourquoi cette fonctionnalité est utile
- **Proposition** : Comment l'implémenter (optionnel)
- **Alternatives** : Solutions existantes considérées

### 📝 Améliorer la Documentation

La documentation est **essentielle** ! Contributions bienvenues pour :
- Corriger typos/erreurs
- Ajouter exemples
- Clarifier sections confuses
- Traduire (anglais/français)

### 🔧 Ajouter des Composants

Si vous ajoutez un nouveau composant :

1. **Structure** :
   ```
   components/mon_composant/
   ├── __init__.py          # Configuration ESPHome
   ├── mon_composant.h      # Header C++
   ├── mon_composant.cpp    # Implémentation
   └── README.md            # Documentation
   ```

2. **Documentation** : Inclure dans `README.md` :
   - Description du composant
   - Configuration YAML
   - Exemples
   - API C++ (si applicable)

3. **Tests** : Ajouter un exemple de configuration dans `examples/`

---

## 🛠️ Processus de Développement

### Environnement de Développement

```bash
# Installation ESPHome
pip install esphome

# Cloner le dépôt
git clone https://github.com/youkorr/test2_esp_video_esphome.git
cd test2_esp_video_esphome

# Tester la compilation
esphome compile TEMPLATE_CONFIG.yaml
```

### Workflow Git

```
main (branche principale)
  ├── feature/nouvelle-fonctionnalite (votre branche)
  ├── fix/correction-bug
  └── docs/amelioration-doc
```

- **`main`** : Branche stable
- **Branches feature** : Nouvelles fonctionnalités
- **Branches fix** : Corrections bugs
- **Branches docs** : Documentation

### Cycle de Review

1. **Ouverture PR** : Vous ouvrez une Pull Request
2. **Review automatique** : CI/CD vérifie la compilation
3. **Review humaine** : Un mainteneur review le code
4. **Changements demandés** : Vous ajustez si nécessaire
5. **Approval** : PR approuvée
6. **Merge** : PR mergée dans `main`

---

## 📏 Standards de Code

### Python (ESPHome Components)

**Style** : PEP 8

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# Constants en MAJUSCULES
CONF_MY_OPTION = "my_option"

# Noms de variables en snake_case
my_namespace = cg.esphome_ns.namespace("my_component")

# Configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.Required(CONF_ID): cv.declare_id(MyComponent),
    cv.Optional(CONF_MY_OPTION, default=True): cv.boolean,
})

async def to_code(config):
    """Generate C++ code from YAML config."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
```

**Vérification** :
```bash
# Formatter avec black
pip install black
black components/mon_composant/__init__.py

# Linter avec pylint
pip install pylint
pylint components/mon_composant/__init__.py
```

### C++ (Implémentation Components)

**Style** : Google C++ Style Guide (adapté ESPHome)

```cpp
#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace my_component {

// Classes en PascalCase
class MyComponent : public Component {
 public:
  // Méthodes publiques
  void setup() override;
  void loop() override;

  // Setters/Getters
  void set_my_option(bool value) { this->my_option_ = value; }
  bool get_my_option() const { return this->my_option_; }

 protected:
  // Variables privées suffixées par _
  bool my_option_{true};

  // Constantes statiques
  static const char *const TAG;
};

}  // namespace my_component
}  // namespace esphome
```

**Bonnes pratiques** :
- ✅ Utiliser `ESP_LOGD/I/W/E` pour logs
- ✅ Libérer la mémoire allouée (`free()`, `delete`)
- ✅ Vérifier pointeurs null avant utilisation
- ✅ Utiliser `const` pour paramètres en lecture seule
- ❌ Éviter `malloc()` (utiliser `new` ou PSRAM helpers)
- ❌ Pas de `delay()` dans `loop()` (utiliser timers)

### YAML (Configuration)

```yaml
# Commentaires descriptifs
my_component:
  # Option importante (default: true)
  my_option: true

  # Liste avec indentation 2 espaces
  items:
    - id: item1
      name: "Item 1"

    - id: item2
      name: "Item 2"
```

**Règles** :
- Indentation : **2 espaces** (pas de tabs)
- IDs : `snake_case`
- Noms affichés : `"Title Case"`
- Commentaires : Expliquer le "pourquoi", pas le "quoi"

---

## 🧪 Tests

### Tests Manuels

1. **Compilation** :
   ```bash
   esphome compile votre_config.yaml
   ```

2. **Flash et Logs** :
   ```bash
   esphome run votre_config.yaml
   esphome logs votre_config.yaml
   ```

3. **Vérifications** :
   - ✅ Compilation sans erreurs
   - ✅ Aucun warning critique
   - ✅ Logs propres (pas d'erreurs runtime)
   - ✅ Fonctionnalité testée sur matériel

### Checklist Avant PR

- [ ] Code compile sans erreurs
- [ ] Code compile sans warnings
- [ ] Tests manuels effectués
- [ ] Documentation mise à jour
- [ ] Exemple de configuration fourni
- [ ] Commit messages clairs
- [ ] Branch à jour avec `main`

---

## 📚 Documentation

### Structure Documentation

```
README.md                          # Vue d'ensemble du projet
QUICK_START.md                     # Démarrage rapide 5 min
TEMPLATE_CONFIG.yaml               # Template configuration
CONTRIBUTING.md                    # Ce fichier

components/
└── mon_composant/
    └── README.md                  # Doc du composant

docs/                              # Guides détaillés
├── MIGRATION_LVGL_V9_README.md
└── OPTIMISATIONS_CAMERA_VIDEO.md
```

### Écrire de la Documentation

**Bon exemple** :
```markdown
## Configuration

Ajoutez cette section à votre YAML :

‍```yaml
mon_composant:
  option1: true  # Active la fonctionnalité X
  option2: 42    # Valeur de Y (default: 42)
‍```

**Résultat** : Le composant va ...
```

**Mauvais exemple** :
```markdown
## Configuration

‍```yaml
mon_composant:
  option1: true
  option2: 42
‍```
```

---

## 📄 Licence

En contribuant, vous acceptez que vos contributions soient sous les mêmes licences que le projet :

- **Composants originaux** : Apache 2.0
- **Code tiers** : Respecter licences existantes (LVGL: MIT, etc.)

---

## 💬 Communication

### Issues GitHub

Pour discussions techniques, bugs, features :
**https://github.com/youkorr/test2_esp_video_esphome/issues**

### Discussions GitHub

Pour questions générales, aide, idées :
**https://github.com/youkorr/test2_esp_video_esphome/discussions**

### ESPHome Discord

Pour support communautaire ESPHome :
**https://discord.gg/esphome**

---

## 🎉 Premiers Pas pour Nouveaux Contributeurs

Idées de contributions faciles pour démarrer :

1. **Documentation** :
   - Corriger typos
   - Ajouter exemples
   - Traduire en anglais

2. **Exemples** :
   - Créer configurations pour nouveaux cas d'usage
   - Tester sur différents boards
   - Documenter résultats

3. **Tests** :
   - Tester composants existants
   - Rapporter bugs
   - Valider sur différents setups

4. **Améliorations mineures** :
   - Améliorer messages logs
   - Ajouter commentaires code
   - Optimiser performances

---

## 🙏 Remerciements

Merci à tous les contributeurs qui rendent ce projet meilleur ! 🎉

**Contributeurs actuels** :
- @youkorr (mainteneur principal)
- Et vous prochainement ? 😊

---

**Questions ?** Ouvrez une **Discussion** sur GitHub ou rejoignez le **Discord ESPHome** !

Happy coding! 🚀
