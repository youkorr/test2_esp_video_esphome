# 🔧 Corrections pour p4mini.yaml

## ❌ Erreur 1 : Ligne 2811 - `is_enabled()` n'existe pas

**Code actuel (INCORRECT) :**
```yaml
- lambda: |-
    bool current = id(security_cam_1).is_enabled();
    id(security_cam_1).set_enabled(!current);
    ESP_LOGI("security", "Camera 1: %s", !current ? "ON" : "OFF");
```

**Solution 1 - Utiliser une variable globale pour suivre l'état :**
```yaml
# En haut de votre fichier YAML, dans la section esphome:
esphome:
  platformio_options:
    # ... vos options existantes

# Ajoutez cette section globals pour suivre l'état des caméras:
globals:
  - id: cam1_state
    type: bool
    initial_value: 'false'
  - id: cam2_state
    type: bool
    initial_value: 'false'
  - id: cam3_state
    type: bool
    initial_value: 'false'
  - id: cam4_state
    type: bool
    initial_value: 'false'

# Puis dans vos boutons toggle:
- button:
    id: btn_cam1_toggle
    on_click:
      then:
        - lambda: |-
            id(cam1_state) = !id(cam1_state);
            id(security_cam_1).set_enabled(id(cam1_state));
            ESP_LOGI("security", "Camera 1: %s", id(cam1_state) ? "ON" : "OFF");
```

**Solution 2 - Toggle simple sans vérifier l'état (RECOMMANDÉ) :**
```yaml
- button:
    id: btn_cam1_toggle
    on_click:
      then:
        - lambda: |-
            // Simplement activer la caméra sans vérifier l'état
            id(security_cam_1).set_enabled(true);
            ESP_LOGI("security", "Camera 1: ON");
```

**Solution 3 - Utiliser un switch au lieu d'un toggle :**
```yaml
# Les switches ont déjà un état interne
switch:
  - platform: network_camera
    name: "Security Camera 1"
    camera_id: security_cam_1
    id: switch_cam1

# Puis dans votre bouton:
- button:
    id: btn_cam1_toggle
    on_click:
      then:
        - switch.toggle: switch_cam1
```

---

## ❌ Erreur 2 : Ligne 2678 - `configure_canvas()` sur mauvais objet

**Code actuel (INCORRECT) :**
```yaml
on_load:
  - lambda: |-
      ESP_LOGI("camera", "Camera page loaded");
      static bool canvas_configured = false;
      if (!canvas_configured) {
        auto canvas = id(camera_canvas);
        if (canvas != nullptr) {
          id(tab5_cam).configure_canvas(canvas);  # ❌ ERREUR ICI
          canvas_configured = true;
          ESP_LOGI("lvgl", "✓ Canvas configured");
        }
      }
```

**Code corrigé :**
```yaml
on_load:
  - lambda: |-
      ESP_LOGI("camera", "Camera page loaded");
      static bool canvas_configured = false;
      if (!canvas_configured) {
        auto canvas = id(camera_canvas);
        if (canvas != nullptr) {
          id(camera_display).configure_canvas(canvas);  # ✓ CORRECT
          canvas_configured = true;
          ESP_LOGI("lvgl", "✓ Canvas configured");
        }
      }
```

**Explication :**
- `tab5_cam` = Composant caméra physique ESP (MIPI DSI)
- `camera_display` = Composant LVGL pour afficher la caméra sur le canvas
- C'est `camera_display` qui a la méthode `configure_canvas()`, pas `tab5_cam`

---

## ✅ Configuration complète corrigée pour la page de sécurité

### Page security avec multi-caméras (CORRIGÉ) :

```yaml
# ========== GLOBALS POUR SUIVRE L'ÉTAT DES CAMÉRAS ==========
globals:
  - id: cam1_state
    type: bool
    initial_value: 'false'
  - id: cam2_state
    type: bool
    initial_value: 'false'
  - id: cam3_state
    type: bool
    initial_value: 'false'
  - id: cam4_state
    type: bool
    initial_value: 'false'

# ========== PAGE SECURITY ==========
- id: security_page
  bg_color: 0x1a1a1a
  on_load:
    - lambda: |-
        ESP_LOGI("security", "🔒 Security page loaded");
        static bool canvas_configured = false;
        if (!canvas_configured) {
          auto canvas = id(security_canvas);
          if (canvas != nullptr) {
            id(security_display).configure_canvas(canvas);  # ✓ CORRECT
            canvas_configured = true;
            ESP_LOGI("security", "✓ Security canvas configured");
          }
        }
  widgets:

    # ==============================================================
    # CANVAS - Grille 2x2 pour 4 caméras
    # ==============================================================
    - canvas:
        id: security_canvas
        width: 640
        height: 480
        x: 10
        y: 10
        bg_color: 0x000000

    # ==============================================================
    # BOUTON RETOUR
    # ==============================================================
    - button:
        id: security_back
        width: 100
        height: 40
        x: 690
        y: 10
        bg_color: 0xe74c3c
        bg_opa: COVER
        radius: 20
        shadow_width: 6
        shadow_color: 0xe74c3c
        shadow_opa: 80%
        on_click:
          then:
            - lambda: |-
                ESP_LOGI("security", "🔙 Stopping all cameras");
                id(security_cam_1).set_enabled(false);
                id(security_cam_2).set_enabled(false);
                id(security_cam_3).set_enabled(false);
                id(security_cam_4).set_enabled(false);
                id(cam1_state) = false;
                id(cam2_state) = false;
                id(cam3_state) = false;
                id(cam4_state) = false;
            - lvgl.page.show: page_home
        widgets:
          - label:
              text: "BACK"
              text_color: 0xFFFFFF
              text_font: nunito_24
              text_align: CENTER
              align: CENTER

    # ==============================================================
    # BOUTON START ALL
    # ==============================================================
    - button:
        id: btn_start_all
        width: 100
        height: 40
        x: 690
        y: 100
        bg_color: 0x27ae60
        bg_opa: COVER
        radius: 20
        shadow_width: 6
        shadow_color: 0x27ae60
        shadow_opa: 80%
        on_click:
          then:
            - lambda: |-
                ESP_LOGI("security", "▶ Starting all cameras");
                id(security_cam_1).set_enabled(true);
                id(security_cam_2).set_enabled(true);
                id(security_cam_3).set_enabled(true);
                id(security_cam_4).set_enabled(true);
                id(cam1_state) = true;
                id(cam2_state) = true;
                id(cam3_state) = true;
                id(cam4_state) = true;
        widgets:
          - label:
              text: "START"
              text_color: 0xFFFFFF
              text_font: nunito_24
              text_align: CENTER
              align: CENTER

    # ==============================================================
    # BOUTON STOP ALL
    # ==============================================================
    - button:
        id: btn_stop_all
        width: 100
        height: 40
        x: 690
        y: 160
        bg_color: 0xe74c3c
        bg_opa: COVER
        radius: 20
        shadow_width: 6
        shadow_color: 0xe74c3c
        shadow_opa: 80%
        on_click:
          then:
            - lambda: |-
                ESP_LOGI("security", "■ Stopping all cameras");
                id(security_cam_1).set_enabled(false);
                id(security_cam_2).set_enabled(false);
                id(security_cam_3).set_enabled(false);
                id(security_cam_4).set_enabled(false);
                id(cam1_state) = false;
                id(cam2_state) = false;
                id(cam3_state) = false;
                id(cam4_state) = false;
        widgets:
          - label:
              text: "STOP"
              text_color: 0xFFFFFF
              text_font: nunito_24
              text_align: CENTER
              align: CENTER

    # ==============================================================
    # BOUTONS INDIVIDUELS (CORRIGÉS)
    # ==============================================================
    - button:
        id: btn_cam1_toggle
        width: 80
        height: 30
        x: 690
        y: 240
        bg_color: 0x3498db
        bg_opa: COVER
        radius: 15
        on_click:
          then:
            - lambda: |-
                id(cam1_state) = !id(cam1_state);
                id(security_cam_1).set_enabled(id(cam1_state));
                ESP_LOGI("security", "Camera 1: %s", id(cam1_state) ? "ON" : "OFF");
        widgets:
          - label:
              text: "CAM 1"
              text_color: 0xFFFFFF
              text_font: nunito_20
              text_align: CENTER
              align: CENTER

    - button:
        id: btn_cam2_toggle
        width: 80
        height: 30
        x: 690
        y: 280
        bg_color: 0x3498db
        bg_opa: COVER
        radius: 15
        on_click:
          then:
            - lambda: |-
                id(cam2_state) = !id(cam2_state);
                id(security_cam_2).set_enabled(id(cam2_state));
                ESP_LOGI("security", "Camera 2: %s", id(cam2_state) ? "ON" : "OFF");
        widgets:
          - label:
              text: "CAM 2"
              text_color: 0xFFFFFF
              text_font: nunito_20
              text_align: CENTER
              align: CENTER

    - button:
        id: btn_cam3_toggle
        width: 80
        height: 30
        x: 690
        y: 320
        bg_color: 0x3498db
        bg_opa: COVER
        radius: 15
        on_click:
          then:
            - lambda: |-
                id(cam3_state) = !id(cam3_state);
                id(security_cam_3).set_enabled(id(cam3_state));
                ESP_LOGI("security", "Camera 3: %s", id(cam3_state) ? "ON" : "OFF");
        widgets:
          - label:
              text: "CAM 3"
              text_color: 0xFFFFFF
              text_font: nunito_20
              text_align: CENTER
              align: CENTER

    - button:
        id: btn_cam4_toggle
        width: 80
        height: 30
        x: 690
        y: 360
        bg_color: 0x3498db
        bg_opa: COVER
        radius: 15
        on_click:
          then:
            - lambda: |-
                id(cam4_state) = !id(cam4_state);
                id(security_cam_4).set_enabled(id(cam4_state));
                ESP_LOGI("security", "Camera 4: %s", id(cam4_state) ? "ON" : "OFF");
        widgets:
          - label:
              text: "CAM 4"
              text_color: 0xFFFFFF
              text_font: nunito_20
              text_align: CENTER
              align: CENTER
```

---

## 📋 Checklist des modifications à faire dans p4mini.yaml

1. ✅ Ajouter la section `globals:` en haut du fichier
2. ✅ Remplacer `id(tab5_cam).configure_canvas(canvas)` par `id(camera_display).configure_canvas(canvas)` (ligne 2678)
3. ✅ Remplacer tous les `id(security_cam_X).is_enabled()` par le système avec variables globales (ligne 2811 et similaires)
4. ✅ Mettre à jour les états globaux quand vous activez/désactivez les caméras

---

## 🎯 Résumé des changements

| Problème | Solution |
|----------|----------|
| `is_enabled()` n'existe pas | Utiliser des variables globales `globals:` pour suivre l'état |
| `tab5_cam.configure_canvas()` incorrect | Utiliser `camera_display.configure_canvas()` |
| Toggle sans état | Ajouter `id(camX_state) = !id(camX_state)` |

---

## 🚀 Prochaines étapes

1. Ouvrez votre fichier `p4mini.yaml`
2. Appliquez ces corrections
3. Compilez à nouveau
4. Les erreurs devraient disparaître !

Si vous voulez que je vous aide à modifier directement le fichier, copiez-le dans ce dépôt et je ferai les changements pour vous ! 😊
