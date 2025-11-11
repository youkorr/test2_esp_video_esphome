# Configuration Optionnelle - Contrôles Caméra en Temps Réel

## Ajustements Dynamiques depuis Home Assistant

Au lieu de forcer des valeurs fixes pour brightness, contrast, saturation, vous pouvez ajouter des contrôles ESPHome `number` pour ajuster en temps réel.

## Configuration YAML Complète

```yaml
# ============================================
# Caméra MIPI DSI
# ============================================
mipi_dsi_cam:
  id: my_cam
  i2c_id: bsp_bus
  sensor_type: ov5647
  sensor_addr: 0x36
  resolution: "800x640"      # Recommandé: 800x640 @ 50 FPS (testov5647 config)
  pixel_format: RGB565
  framerate: 50

# ============================================
# Contrôles Caméra Optionnels (ajustables depuis Home Assistant)
# ============================================

# Brightness: -128 à 127 (défaut: 0, recommandé: 60 de testov5647)
number:
  - platform: template
    name: "Camera Brightness"
    id: camera_brightness
    min_value: -128
    max_value: 127
    step: 1
    initial_value: 60       # Valeur de testov5647
    optimistic: true
    icon: "mdi:brightness-6"
    entity_category: config
    on_value:
      then:
        - lambda: |-
            id(my_cam).set_brightness((int)x);
            ESP_LOGI("camera_controls", "Brightness set to %d", (int)x);

  # Contrast: 0 à 255 (défaut: 128, recommandé: 145 de testov5647)
  - platform: template
    name: "Camera Contrast"
    id: camera_contrast
    min_value: 0
    max_value: 255
    step: 1
    initial_value: 145      # Valeur de testov5647
    optimistic: true
    icon: "mdi:contrast-box"
    entity_category: config
    on_value:
      then:
        - lambda: |-
            id(my_cam).set_contrast((int)x);
            ESP_LOGI("camera_controls", "Contrast set to %d", (int)x);

  # Saturation: 0 à 255 (défaut: 128, recommandé: 135 de testov5647)
  - platform: template
    name: "Camera Saturation"
    id: camera_saturation
    min_value: 0
    max_value: 255
    step: 1
    initial_value: 135      # Valeur de testov5647
    optimistic: true
    icon: "mdi:palette"
    entity_category: config
    on_value:
      then:
        - lambda: |-
            id(my_cam).set_saturation((int)x);
            ESP_LOGI("camera_controls", "Saturation set to %d", (int)x);

  # Hue: -180 à 180 (défaut: 0)
  - platform: template
    name: "Camera Hue"
    id: camera_hue
    min_value: -180
    max_value: 180
    step: 1
    initial_value: 0
    optimistic: true
    icon: "mdi:palette-advanced"
    entity_category: config
    on_value:
      then:
        - lambda: |-
            id(my_cam).set_hue((int)x);
            ESP_LOGI("camera_controls", "Hue set to %d", (int)x);

  # Sharpness: 0 à 255 (défaut: 128)
  - platform: template
    name: "Camera Sharpness"
    id: camera_sharpness
    min_value: 0
    max_value: 255
    step: 1
    initial_value: 128
    optimistic: true
    icon: "mdi:image-filter-center-focus"
    entity_category: config
    on_value:
      then:
        - lambda: |-
            id(my_cam).set_sharpness((int)x);
            ESP_LOGI("camera_controls", "Sharpness set to %d", (int)x);

# ============================================
# Contrôles White Balance
# ============================================

# Switch pour activer/désactiver AWB
switch:
  - platform: template
    name: "Camera Auto White Balance"
    id: camera_awb
    optimistic: true
    restore_mode: RESTORE_DEFAULT_ON
    icon: "mdi:white-balance-auto"
    entity_category: config
    turn_on_action:
      - lambda: |-
          id(my_cam).set_white_balance_mode(true);
          ESP_LOGI("camera_controls", "AWB enabled");
    turn_off_action:
      - lambda: |-
          id(my_cam).set_white_balance_mode(false);
          ESP_LOGI("camera_controls", "AWB disabled");

# White Balance Temperature (si AWB désactivé)
number:
  - platform: template
    name: "Camera White Balance Temperature"
    id: camera_wb_temp
    min_value: 2800
    max_value: 6500
    step: 100
    initial_value: 5500     # Lumière du jour
    optimistic: true
    unit_of_measurement: "K"
    icon: "mdi:thermometer"
    entity_category: config
    on_value:
      then:
        - lambda: |-
            // Désactiver AWB avant d'appliquer température manuelle
            id(my_cam).set_white_balance_mode(false);
            id(my_cam).set_white_balance_temp((int)x);
            ESP_LOGI("camera_controls", "WB Temperature set to %dK", (int)x);

# ============================================
# Contrôles Exposition Manuels (Optionnels)
# ============================================

# Switch pour activer/désactiver AEC (Auto Exposure Control)
switch:
  - platform: template
    name: "Camera Auto Exposure"
    id: camera_aec
    optimistic: true
    restore_mode: RESTORE_DEFAULT_ON
    icon: "mdi:brightness-auto"
    entity_category: config
    turn_on_action:
      - lambda: |-
          id(my_cam).set_exposure(0);  // 0 = enable auto exposure
          ESP_LOGI("camera_controls", "AEC enabled");
    turn_off_action:
      - lambda: |-
          // Désactiver AEC, appliquer valeur manuelle
          ESP_LOGI("camera_controls", "AEC disabled, use manual exposure slider");

# Exposition manuelle (si AEC désactivé)
number:
  - platform: template
    name: "Camera Manual Exposure"
    id: camera_exposure
    min_value: 100
    max_value: 50000
    step: 100
    initial_value: 15000    # Exposition normale
    optimistic: true
    icon: "mdi:camera-timer"
    entity_category: config
    on_value:
      then:
        - lambda: |-
            id(my_cam).set_exposure((int)x);
            ESP_LOGI("camera_controls", "Manual exposure set to %d", (int)x);

  # Gain manuel
  - platform: template
    name: "Camera Manual Gain"
    id: camera_gain
    min_value: 1000
    max_value: 16000
    step: 100
    initial_value: 8000     # 8x gain
    optimistic: true
    icon: "mdi:amplifier"
    entity_category: config
    on_value:
      then:
        - lambda: |-
            id(my_cam).set_gain((int)x);
            ESP_LOGI("camera_controls", "Manual gain set to %d (%.1fx)", (int)x, x/1000.0);

# ============================================
# Boutons de Réinitialisation
# ============================================

button:
  # Reset aux valeurs de testov5647 (brightness: 60, contrast: 145, saturation: 135)
  - platform: template
    name: "Camera Reset to testov5647 Settings"
    icon: "mdi:restore"
    entity_category: config
    on_press:
      - lambda: |-
          id(camera_brightness).publish_state(60);
          id(my_cam).set_brightness(60);
          id(camera_contrast).publish_state(145);
          id(my_cam).set_contrast(145);
          id(camera_saturation).publish_state(135);
          id(my_cam).set_saturation(135);
          id(camera_hue).publish_state(0);
          id(my_cam).set_hue(0);
          id(camera_sharpness).publish_state(128);
          id(my_cam).set_sharpness(128);
          ESP_LOGI("camera_controls", "Reset to testov5647 settings: Brightness=60, Contrast=145, Saturation=135");

  # Reset aux valeurs par défaut (brightness: 0, contrast: 128, saturation: 128)
  - platform: template
    name: "Camera Reset to Defaults"
    icon: "mdi:restore-alert"
    entity_category: config
    on_press:
      - lambda: |-
          id(camera_brightness).publish_state(0);
          id(my_cam).set_brightness(0);
          id(camera_contrast).publish_state(128);
          id(my_cam).set_contrast(128);
          id(camera_saturation).publish_state(128);
          id(my_cam).set_saturation(128);
          id(camera_hue).publish_state(0);
          id(my_cam).set_hue(0);
          id(camera_sharpness).publish_state(128);
          id(my_cam).set_sharpness(128);
          ESP_LOGI("camera_controls", "Reset to defaults: Brightness=0, Contrast=128, Saturation=128");
```

## Utilisation dans Home Assistant

Une fois cette configuration flashée, vous verrez dans Home Assistant:

### Contrôles Nombre (Number):
- **Camera Brightness** (défaut: 60)
- **Camera Contrast** (défaut: 145)
- **Camera Saturation** (défaut: 135)
- **Camera Hue** (défaut: 0)
- **Camera Sharpness** (défaut: 128)
- **Camera White Balance Temperature** (défaut: 5500K)
- **Camera Manual Exposure** (défaut: 15000)
- **Camera Manual Gain** (défaut: 8000)

### Contrôles Switch:
- **Camera Auto White Balance** (défaut: ON)
- **Camera Auto Exposure** (défaut: ON)

### Boutons:
- **Camera Reset to testov5647 Settings** (60/145/135)
- **Camera Reset to Defaults** (0/128/128)

## Ajustement en Temps Réel

Vous pouvez créer une carte Lovelace pour contrôler facilement:

```yaml
# Dans votre dashboard Home Assistant
type: vertical-stack
cards:
  - type: entities
    title: Contrôles Caméra
    entities:
      - entity: switch.camera_auto_white_balance
      - entity: switch.camera_auto_exposure

  - type: entities
    title: Qualité d'Image
    entities:
      - entity: number.camera_brightness
        name: Luminosité
      - entity: number.camera_contrast
        name: Contraste
      - entity: number.camera_saturation
        name: Saturation
      - entity: number.camera_hue
        name: Teinte
      - entity: number.camera_sharpness
        name: Netteté

  - type: entities
    title: Balance des Blancs
    entities:
      - entity: number.camera_white_balance_temperature
        name: Température Couleur

  - type: entities
    title: Exposition Manuelle
    entities:
      - entity: number.camera_manual_exposure
        name: Exposition
      - entity: number.camera_manual_gain
        name: Gain

  - type: entities
    title: Réinitialisation
    entities:
      - entity: button.camera_reset_to_testov5647_settings
      - entity: button.camera_reset_to_defaults
```

## Désactivation de l'Auto-Application

Si vous utilisez ces contrôles YAML, vous voudrez peut-être **désactiver l'auto-application** des valeurs fixes dans le code.

Pour cela, commentez ces lignes dans `mipi_dsi_cam.cpp` (lignes 1025-1044):

```cpp
// OPTIONNEL: Désactiver si vous utilisez les contrôles YAML
/*
// Auto-boost brightness (basé sur config testov5647 qui fonctionnait)
if (this->set_brightness(60)) {
  ESP_LOGI(TAG, "✓ Brightness boosted to 60 (from testov5647 working config)");
} else {
  ESP_LOGW(TAG, "⚠️  Failed to boost brightness");
}

// Auto-boost contraste (basé sur config testov5647: 145)
if (this->set_contrast(145)) {
  ESP_LOGI(TAG, "✓ Contrast set to 145 (from testov5647 working config)");
} else {
  ESP_LOGW(TAG, "⚠️  Failed to set contrast");
}

// Auto-boost saturation (basé sur config testov5647: 135)
if (this->set_saturation(135)) {
  ESP_LOGI(TAG, "✓ Saturation set to 135 (from testov5647 working config)");
} else {
  ESP_LOGW(TAG, "⚠️  Failed to set saturation");
}
*/
```

Ou mieux, gardez l'auto-application AWB seulement:

```cpp
// Auto-activer AWB (Auto White Balance) pour corriger blanc → jaune
if (this->set_white_balance_mode(true)) {
  ESP_LOGI(TAG, "✓ AWB (Auto White Balance) enabled");
} else {
  ESP_LOGW(TAG, "⚠️  Failed to enable AWB, trying manual white balance temperature");
  this->set_white_balance_temp(5500);
}

// Les autres valeurs seront appliquées via les contrôles YAML initial_value
```

## Avantages de cette Approche

1. ✅ **Ajustement en temps réel** depuis Home Assistant
2. ✅ **Pas besoin de recompiler** pour tester différentes valeurs
3. ✅ **Sauvegarde des préférences** (restore_mode)
4. ✅ **Reset facile** aux valeurs testov5647 ou défaut
5. ✅ **Contrôle total** sur tous les paramètres caméra
6. ✅ **Visualisation instantanée** des changements sur écran/web

## Exemple d'Utilisation Typique

1. **Démarrage**: Les valeurs `initial_value` (60/145/135) sont appliquées automatiquement
2. **Ajustement**: Vous ajustez via Home Assistant en temps réel
3. **Trop lumineux**: Réduire brightness de 60 → 40
4. **Blanc toujours jaune**: Désactiver AWB, tester température 6500K (plus froid)
5. **Manque de contraste**: Augmenter contrast de 145 → 160
6. **Satisfaction**: Les valeurs sont sauvegardées et restaurées au reboot

## Notes Importantes

- Les contrôles sont dans `entity_category: config` = apparaissent dans la section Configuration de Home Assistant
- `optimistic: true` = pas de feedback, on assume que la commande a réussi
- Les valeurs `initial_value` correspondent à testov5647 (60/145/135)
- AWB et AEC sont activés par défaut (`restore_mode: RESTORE_DEFAULT_ON`)
