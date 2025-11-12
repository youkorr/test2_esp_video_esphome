# ESP-DL Face Detection Models for ESP32-P4

Ce dossier doit contenir les modèles de détection de visages au format `.espdl` pour ESP32-P4.

## Modèles Requis

Les fichiers suivants sont nécessaires pour la détection de visages :

1. **human_face_detect_msr_s8_v1.espdl** - Modèle MSR (Multi-Scale Region)
   - Entrée : 120x160 RGB
   - Détection initiale des visages

2. **human_face_detect_mnp_s8_v1.espdl** - Modèle MNP (Multi-Neck Post-processing)
   - Entrée : 48x48 RGB
   - Affinage des résultats de détection

## Où Obtenir les Modèles

### Option 1 : Télécharger depuis Waveshare (Recommandé)

```bash
cd components/human_face_detect/models/p4/
wget https://raw.githubusercontent.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B/main/examples/ESP-IDF/11_esp_brookesia_phone/components/human_face_detect/models/p4/human_face_detect_msr_s8_v1.espdl
wget https://raw.githubusercontent.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B/main/examples/ESP-IDF/11_esp_brookesia_phone/components/human_face_detect/models/p4/human_face_detect_mnp_s8_v1.espdl
```

### Option 2 : Depuis esp-dl Repository

Les modèles sont également disponibles dans le dépôt officiel esp-dl d'Espressif :
https://github.com/espressif/esp-dl/tree/master/models/human_face_detect

### Option 3 : Générer avec esp-dl Tools

Si vous avez des modèles personnalisés, vous pouvez les convertir au format `.espdl` :
https://github.com/espressif/esp-dl/tree/master/tools

## Vérification

Après téléchargement, vérifiez que les fichiers sont présents :

```bash
ls -lh components/human_face_detect/models/p4/
# Devrait afficher :
# human_face_detect_msr_s8_v1.espdl
# human_face_detect_mnp_s8_v1.espdl
```

## Structure du Build

Les fichiers `.espdl` seront automatiquement inclus dans le firmware lors de la compilation ESP-IDF via le système de build du composant.

## Taille des Modèles

- **MSR (s8 v1)** : ~500 KB
- **MNP (s8 v1)** : ~50 KB
- **Total** : ~550 KB de flash requis

## License

Les modèles sont sous licence MIT (Espressif Systems).

Référence : https://github.com/espressif/esp-dl/blob/master/LICENSE
