# ESP-DL Face Detection Models for ESP32-P4

Ce dossier contient les modèles de détection de visages pour le composant `human_face_detect`.

## 🚀 Démarrage Rapide - Téléchargement Automatique

Utilisez le script fourni pour télécharger et optionnellement empaqueter les modèles:

```bash
cd components/human_face_detect/models
./download_and_pack.sh
```

Le script va:
1. Télécharger les modèles MSR et MNP depuis ESP-DL v3.1.0
2. Demander si vous voulez les empaqueter en un seul fichier
3. Vous guider pour les copier sur la carte SD

## 📦 Option 1: Fichier Unique Empaqueté (Recommandé)

**Avantages:**
- ✅ Un seul fichier à gérer
- ✅ Copie plus rapide sur carte SD
- ✅ Moins d'encombrement

**Utilisation:**
```bash
./download_and_pack.sh
# Répondre 'y' pour empaqueter

# Copier sur carte SD:
cp human_face_detect.espdl /chemin/vers/sdcard/
```

**Configuration ESPHome:**
```yaml
human_face_detect:
  camera: my_cam
  enable_detection: true
  model_dir: "/sdcard"
  # Utilise le fichier empaqueté - ESP-DL extraira les deux modèles
```

## 📁 Option 2: Fichiers Séparés

**Avantages:**
- ✅ Mise à jour individuelle des modèles possible
- ✅ Plus flexible

**Utilisation:**
```bash
./download_and_pack.sh
# Répondre 'n' pour garder séparés

# Copier les deux fichiers sur carte SD:
cp human_face_detect_msr_s8_v1.espdl /chemin/vers/sdcard/
cp human_face_detect_mnp_s8_v1.espdl /chemin/vers/sdcard/
```

**Configuration ESPHome:**
```yaml
human_face_detect:
  camera: my_cam
  enable_detection: true
  model_dir: "/sdcard"
  msr_model_file: "human_face_detect_msr_s8_v1.espdl"
  mnp_model_file: "human_face_detect_mnp_s8_v1.espdl"
```

## 📥 Téléchargement Manuel

Si vous préférez télécharger manuellement:

### Modèle MSR (Détection Multi-Échelle)
```bash
wget https://github.com/espressif/esp-dl/raw/v3.1.0/models/human_face_detect/human_face_detect_msr_s8_v1.espdl
```
Taille: ~200 KB

### Modèle MNP (Post-Traitement Multi-Cou)
```bash
wget https://github.com/espressif/esp-dl/raw/v3.1.0/models/human_face_detect/human_face_detect_mnp_s8_v1.espdl
```
Taille: ~150 KB

## 🔧 Empaquetage Manuel (Optionnel)

Pour empaqueter les modèles vous-même:

```bash
python3 pack_model.py \
  -m human_face_detect_msr_s8_v1.espdl human_face_detect_mnp_s8_v1.espdl \
  -o human_face_detect.espdl
```

## 📊 Détails des Modèles

| Modèle | Type | Taille | Rôle |
|--------|------|--------|------|
| **MSR** | Détection | ~200 KB | 1ère étape: détecte les candidats visages |
| **MNP** | Raffinement | ~150 KB | 2ème étape: affine les boîtes englobantes |
| **Empaqueté** | Combiné | ~350 KB | Les deux modèles en un seul fichier |

## 💾 Structure de la Carte SD

### Avec Fichier Empaqueté:
```
/sdcard/
└── human_face_detect.espdl  (350 KB)
```

### Avec Fichiers Séparés:
```
/sdcard/
├── human_face_detect_msr_s8_v1.espdl  (200 KB)
└── human_face_detect_mnp_s8_v1.espdl  (150 KB)
```

## 🔍 Dépannage

### Erreur "models not found"
```
[human_face_detect] ❌ MSR model file not found: /sdcard/human_face_detect_msr_s8_v1.espdl
```

**Solutions:**
1. Vérifier que la carte SD est montée (`ls /sdcard`)
2. Vérifier que les fichiers modèles existent sur la carte SD
3. Vérifier les permissions (doivent être lisibles)
4. Vérifier le bon `model_dir` dans la config YAML

### Erreur "Wrong model format"
```
RuntimeError: Wrong model format.
```

**Solution:**
Re-télécharger les modèles - ils sont peut-être corrompus. Utilisez `download_and_pack.sh`.

## 📌 Versions des Modèles

Actuellement supporté: **ESP-DL v3.1.0**

Pour d'autres versions, modifiez la variable `ESPDL_VERSION` dans `download_and_pack.sh`.

## 🔗 Références

- Dépôt ESP-DL: https://github.com/espressif/esp-dl
- Source des Modèles: https://github.com/espressif/esp-dl/tree/master/models/human_face_detect
- Référence Waveshare: https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B
- Outil pack_model.py: Adapté de Waveshare

## 📄 License

Les modèles sont sous licence MIT (Espressif Systems).

Référence: https://github.com/espressif/esp-dl/blob/master/LICENSE
