# Désactiver Temporairement human_face_detect

## Problème

human_face_detect nécessite des configurations ESP-IDF complexes qui causent actuellement:
1. Watchdog timeout pendant le chargement des modèles
2. esp-dl ne peut pas être installé via Library Manager ESPHome

## Solution Temporaire: Désactiver face detection

Dans votre fichier YAML ESPHome, modifiez:

```yaml
# Désactiver temporairement face detection
human_face_detect:
  id: face_detector
  camera: tab5_cam
  enable_detection: false  # ← Changé de true à false
  confidence_threshold: 0.5
  model_type: MSRMNP_S8_V1
```

**OU** commentez complètement la section:

```yaml
# ============================================================================
# Human Face Detect - TEMPORAIREMENT DÉSACTIVÉ
# ============================================================================    
#
# human_face_detect:
#   id: face_detector
#   camera: tab5_cam
#   enable_detection: true
#   confidence_threshold: 0.5
#   model_type: MSRMNP_S8_V1
```

## Également dans lvgl_camera_display et camera_web_server

Commentez la ligne `face_detector`:

```yaml
lvgl_camera_display:
  id: camera_display
  camera_id: tab5_cam
  canvas_id: camera_canvas
  update_interval: 10ms
  # face_detector: face_detector  # ← Commenté temporairement

camera_web_server:
  camera_id: tab5_cam
  port: 8080
  enable_stream: true
  enable_snapshot: false
  # face_detector: face_detector  # ← Commenté temporairement
```

## Résultat

Après cette modification:
- ✅ Le firmware compilera sans erreur
- ✅ Pas de watchdog timeout
- ✅ Vous pourrez tester le **fix du crop 800x640**
- ✅ LVGL camera display et web server fonctionneront normalement

## Tester le Fix 800x640

Avec face detection désactivée, vous pourrez vérifier si:
- L'image à 800x640 est maintenant **centrée** (pas à droite)
- Fonctionne sur l'écran LVGL ET le serveur web

## Réactiver plus tard

Une fois le crop testé et validé, on pourra travailler sur l'intégration correcte d'ESP-DL qui nécessite:
1. Configuration ESP-IDF sdkconfig
2. Partition SPIFFS personnalisée
3. Embedment des modèles au build time
