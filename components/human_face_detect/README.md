# Human Face Detection Component (Optional)

Composant ESPHome pour la détection de visages humains sur ESP32-P4, basé sur l'implémentation Waveshare.

## ⚠️ Statut

**Structure créée mais non implémentée** - Ce composant nécessite l'intégration de `esp-dl` (Espressif Deep Learning Library) qui n'est pas encore complète.

## 📋 Prérequis

- ESP32-P4 (AI acceleration)
- esp-idf framework
- esp-dl library (Espressif Deep Learning)
- Modèles de détection :
  - MSR (Multi-Scale Region) : entrée 120x160 RGB
  - MNP (Multi-Neck Post-processing) : entrée 48x48 RGB

## 🎯 Fonctionnalités Prévues

- Détection de visages en temps réel
- Support de plusieurs visages simultanés
- Seuil de confiance configurable
- Coordonnées des rectangles de détection (x, y, w, h)

## 📦 Configuration YAML (Exemple)

```yaml
# Composant optionnel - désactivé par défaut
human_face_detect:
  id: face_detector
  camera: tab5_cam  # Référence vers mipi_dsi_cam
  enable_detection: false  # true pour activer
  confidence_threshold: 0.5
  model_type: MSRMNP_S8_V1
```

## 🔧 API C++

```cpp
// Dans votre code Lambda
if (id(face_detector).is_detection_enabled()) {
  int face_count = id(face_detector).detect_faces();

  if (face_count > 0) {
    int x, y, w, h;
    float confidence;

    if (id(face_detector).get_face_box(0, x, y, w, h, confidence)) {
      ESP_LOGI("app", "Face detected at (%d,%d) size %dx%d conf=%.2f",
               x, y, w, h, confidence);
    }
  }
}
```

## 📚 Référence

Basé sur le code Waveshare :
https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B/tree/main/examples/ESP-IDF/11_esp_brookesia_phone/components/human_face_detect

## 🚧 TODO - Implémentation

- [ ] Intégrer esp-dl library
- [ ] Charger les modèles MSR+MNP
- [ ] Implémenter `init_model_()`
- [ ] Implémenter `detect_faces()`
- [ ] Implémenter `get_face_box()`
- [ ] Gestion de la résolution d'entrée (resize frame)
- [ ] Optimiser les performances
- [ ] Ajouter des sensors ESPHome (face_count, etc.)

## 🤝 Contribution

Contributions bienvenues ! Ce composant fournit la structure de base pour intégrer esp-dl.

## 📄 License

ESPRESSIF MIT (comme le code source Waveshare)
