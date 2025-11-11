# Corrections des Problèmes OV5647 - Blanc → Jaune et Contraste

## Date: 2025-11-11

## Problèmes Rapportés par l'Utilisateur

1. **Double PPA**: Le web_server et LVGL appellent tous deux `capture_frame()` qui génère des erreurs PPA "exceed maximum pending transactions"
2. **Blanc → Jaune**: La lumière blanche de la cuisine apparaît jaune à l'écran et sur le web
3. **Manque de contraste**: L'image est plate et manque de punch

## Corrections Appliquées

### 1. Fix Double PPA - Augmentation de max_pending_trans_num

**Fichier**: `components/mipi_dsi_cam/mipi_dsi_cam.cpp` ligne 217

**Avant:**
```cpp
ppa_config.max_pending_trans_num = 4;  // Insuffisant pour web + LVGL + concurrent calls
```

**Après:**
```cpp
ppa_config.max_pending_trans_num = 16;  // Support web stream + LVGL display + multiple clients
```

**Explication:**
- Le PPA (Pixel-Processing Accelerator) est utilisé pour mirror/rotate hardware
- Chaque appel à `capture_frame()` fait un PPA transform si mirror/rotate activé
- Le web_server ET LVGL appellent `capture_frame()` simultanément
- Avec max_pending_trans_num=4, les transformations concurrentes dépassaient la limite
- Augmentation à 16 permet plus de transformations en parallèle sans erreurs

**Résultat attendu:**
- ✅ Plus d'erreurs "exceed maximum pending transactions"
- ✅ Plus de watchdog timeout
- ✅ Web stream et LVGL display fonctionnent simultanément

---

### 2. Fix Blanc → Jaune - Auto-activation AWB

**Fichier**: `components/mipi_dsi_cam/mipi_dsi_cam.cpp` lignes 1016-1023

**Code ajouté:**
```cpp
// Auto-activer AWB (Auto White Balance) pour corriger blanc → jaune
if (this->set_white_balance_mode(true)) {
  ESP_LOGI(TAG, "✓ AWB (Auto White Balance) enabled");
} else {
  ESP_LOGW(TAG, "⚠️  Failed to enable AWB, trying manual white balance temperature");
  // Fallback: configurer température couleur manuelle (5500K = lumière du jour)
  this->set_white_balance_temp(5500);
}
```

**Explication:**
- Le problème blanc → jaune est causé par une balance des blancs incorrecte
- L'OV5647 a des registres AWB hardware (0x5180-0x519c) mais avec des valeurs fixes
- Ces valeurs fixes ne correspondent pas à l'éclairage de cuisine de l'utilisateur
- L'activation de V4L2_CID_AUTO_WHITE_BALANCE permet à l'ISP/IPA d'ajuster dynamiquement
- Fallback: Si AWB échoue, température couleur manuelle à 5500K (lumière du jour)

**Comment ça fonctionne:**
1. Au démarrage du streaming, attendre 100ms que le stream soit stable
2. Activer AWB via `set_white_balance_mode(true)` → ioctl V4L2_CID_AUTO_WHITE_BALANCE = 1
3. L'ISP/IPA analyse les premières frames et ajuste les gains R/G/B
4. Après quelques secondes, AWB converge et le blanc apparaît correctement

**Résultat attendu:**
- ✅ Blanc de la cuisine apparaît blanc (pas jaune)
- ⏱️ Convergence AWB en 2-5 secondes après démarrage
- 🔄 Adaptation automatique aux changements d'éclairage

---

### 3. Fix Contraste Faible - Auto-boost Contraste et Saturation

**Fichier**: `components/mipi_dsi_cam/mipi_dsi_cam.cpp` lignes 1025-1037

**Code ajouté:**
```cpp
// Auto-boost brightness (basé sur config testov5647 qui fonctionnait)
if (this->set_brightness(60)) {  // 60 au lieu de 0 (défaut) - valeur de testov5647
  ESP_LOGI(TAG, "✓ Brightness boosted to 60 (from testov5647 working config)");
} else {
  ESP_LOGW(TAG, "⚠️  Failed to boost brightness");
}

// Auto-boost contraste (basé sur config testov5647: 145)
if (this->set_contrast(145)) {  // 145 - valeur exacte de testov5647
  ESP_LOGI(TAG, "✓ Contrast set to 145 (from testov5647 working config)");
} else {
  ESP_LOGW(TAG, "⚠️  Failed to set contrast");
}

// Auto-boost saturation (basé sur config testov5647: 135)
if (this->set_saturation(135)) {  // 135 - valeur exacte de testov5647
  ESP_LOGI(TAG, "✓ Saturation set to 135 (from testov5647 working config)");
} else {
  ESP_LOGW(TAG, "⚠️  Failed to set saturation");
}
```

**Explication:**
- OV5647 n'a pas de registres hardware pour le contraste/saturation
- Ces paramètres doivent être ajustés via V4L2 controls au niveau ISP
- **Valeurs basées sur testov5647 (implémentation précédente qui fonctionnait):**
  - Brightness: 60 (défaut: 0, échelle: -128 à 127)
  - Contrast: 145 (défaut: 128, échelle: 0-255) = +13% contraste
  - Saturation: 135 (défaut: 128, échelle: 0-255) = +5.5% saturation
- Ces valeurs ont été testées et produisent une image correcte avec bon contraste

**Résultat attendu:**
- ✅ Image plus lumineuse (brightness +60)
- ✅ Image plus contrastée, moins plate (contrast +13%)
- ✅ Noirs plus profonds, blancs plus éclatants
- ✅ Couleurs légèrement plus saturées (+5.5%)
- ✅ Qualité d'image similaire à testov5647 (mais avec meilleur FPS grâce aux V4L2 controls)

---

## Ordre d'Initialisation au Démarrage du Streaming

```cpp
start_streaming()
  ↓
[Streaming V4L2 démarré]
  ↓
[Attendre 100ms pour stabilisation]
  ↓
[1. Appliquer gains RGB CCM si configurés dans YAML]
  ↓
[2. ✅ NOUVEAU: Activer AWB (Auto White Balance)]
  ↓
[3. ✅ NOUVEAU: Boost Brightness à 60 (testov5647)]
  ↓
[4. ✅ NOUVEAU: Set Contraste à 145 (testov5647)]
  ↓
[5. ✅ NOUVEAU: Set Saturation à 135 (testov5647)]
  ↓
[Streaming prêt avec corrections appliquées]
```

---

## Logs Attendus au Démarrage

Après avoir flashé le nouveau firmware, vous devriez voir ces logs:

```
[I][mipi_dsi_cam:712]: ✅ Using CUSTOM format: 1024x600 RAW8 @ 30fps (OV5647)
[I][mipi_dsi_cam:721]: ✅ Custom format applied successfully!
[I][mipi_dsi_cam:958]: mipi_dsi_cam: streaming started
[I][mipi_dsi_cam:1018]: ✓ AWB (Auto White Balance) enabled                       ← NOUVEAU
[I][mipi_dsi_cam:1027]: ✓ Brightness boosted to 60 (testov5647 config)       ← NOUVEAU
[I][mipi_dsi_cam:1034]: ✓ Contrast set to 145 (testov5647 config)            ← NOUVEAU
[I][mipi_dsi_cam:1041]: ✓ Saturation set to 135 (testov5647 config)          ← NOUVEAU
```

**Si vous voyez des warnings:**
```
[W][mipi_dsi_cam:1020]: ⚠️  Failed to enable AWB, trying manual white balance temperature
```
Cela signifie que V4L2_CID_AUTO_WHITE_BALANCE n'est pas supporté par le driver, mais le fallback température couleur 5500K sera appliqué.

---

## Ajustements Manuels Possibles

Si les corrections automatiques ne suffisent pas, vous pouvez ajuster manuellement via Home Assistant:

### Ajuster le Contraste
```yaml
# Dans votre YAML ESPHome
number:
  - platform: template
    name: "Camera Contrast"
    min_value: 0
    max_value: 255
    step: 1
    initial_value: 160
    optimistic: true
    set_action:
      - lambda: |-
          id(my_cam).set_contrast(x);
```

### Ajuster la Température Couleur AWB
```yaml
number:
  - platform: template
    name: "Camera White Balance Temperature"
    min_value: 2800
    max_value: 6500
    step: 100
    initial_value: 5500
    optimistic: true
    unit_of_measurement: "K"
    set_action:
      - lambda: |-
          id(my_cam).set_white_balance_mode(false);  // Désactiver auto AWB
          id(my_cam).set_white_balance_temp(x);
```

### Ajuster les Gains RGB CCM
```yaml
# Pour corriger une dominante de couleur spécifique
mipi_dsi_cam:
  id: my_cam
  # ... autres configs ...
  rgb_gains:
    red: 1.1      # +10% rouge si image trop cyan
    green: 0.95   # -5% vert si image trop verte
    blue: 1.15    # +15% bleu si image trop jaune
```

---

## Test avec SC202CS

**IMPORTANT**: Ces corrections s'appliquent aussi au SC202CS!

Quand vous testerez le SC202CS, vérifiez que:
1. ✅ Format custom VGA 640x480 appliqué (voir logs)
2. ✅ AWB activé automatiquement
3. ✅ Contraste et saturation boostés
4. ⚠️ Si l'image est trop lumineuse: l'AEC (Auto Exposure Control) devrait converger après quelques secondes

**Logs SC202CS attendus:**
```
[I][mipi_dsi_cam:737]: ✅ Using CUSTOM format: VGA 640x480 RAW8 @ 30fps (SC202CS)
[I][mipi_dsi_cam:746]: ✅ Custom format applied successfully!
[I][mipi_dsi_cam:1018]: ✓ AWB (Auto White Balance) enabled
[I][mipi_dsi_cam:1027]: ✓ Contrast boosted to 160 (+25%)
[I][mipi_dsi_cam:1034]: ✓ Saturation boosted to 144 (+12.5%)
```

---

## Résumé des Fichiers Modifiés

| Fichier | Lignes Modifiées | Description |
|---------|------------------|-------------|
| `mipi_dsi_cam.cpp` | 217 | PPA max_pending_trans_num: 4 → 16 |
| `mipi_dsi_cam.cpp` | 1003-1039 | Auto-activation AWB + Boost contraste/saturation |

---

## Timeline de Convergence

Après le démarrage du streaming:
- **t=0s**: Streaming démarre, custom format appliqué
- **t=0.1s**: AWB activé, contraste/saturation boostés
- **t=0-2s**: AWB analyse les premières frames (blanc peut être jaune)
- **t=2-5s**: AWB converge, blanc devient correctement blanc ✅
- **t=5s+**: AWB stable, adaptation continue aux changements d'éclairage

---

## Problèmes Connus et Solutions

### Problème: Blanc toujours jaune après 10 secondes
**Solution**: AWB n'a pas convergé correctement
1. Désactiver auto AWB: `my_cam.set_white_balance_mode(false)`
2. Essayer température couleur plus froide: `my_cam.set_white_balance_temp(6500)` (6500K = ciel nuageux, plus bleu)

### Problème: Image trop contrastée
**Solution**: Réduire le contraste
```cpp
my_cam.set_contrast(140);  // Au lieu de 160
```

### Problème: Couleurs trop saturées/artificielles
**Solution**: Réduire la saturation
```cpp
my_cam.set_saturation(128);  // Retour à défaut
```

### Problème: PPA errors persistent
**Solution**: Désactiver mirror/rotate si pas nécessaire
```yaml
mipi_dsi_cam:
  # ... autres configs ...
  # Ne PAS définir mirror_x, mirror_y, rotation
  # Le PPA ne sera pas initialisé
```

---

## Références

- **V4L2 Controls Documentation**: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/control.html
- **OV5647 Datasheet**: Registres AWB 0x5180-0x519c
- **ESP32-P4 PPA**: Pixel-Processing Accelerator pour transforms hardware
- **ESPHome Camera**: https://esphome.io/components/camera/
