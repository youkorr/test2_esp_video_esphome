# Face Unlock System - ESPHome LVGL

Systeme de deverrouillage par reconnaissance faciale + Code PIN pour ESP32-P4.

## Architecture

```
page_home (ecran actif)
     |
     | Inactivite (auto_off secondes)
     v
page_sleep (ecran noir, display_lock=true, switch=ON)
     |
     | Touch
     v
face_unlock_page (camera + clavier PIN)
     |
     +--- Face reconnue --> switch=OFF --> page_home
     |
     +--- PIN correct ----> switch=OFF --> page_home
     |
     +--- Timeout --------> page_sleep
```

---

## 1. Substitutions

```yaml
substitutions:
  # Code PIN pour deverrouiller l'ecran (different du code alarme)
  unlock_pin_code: "0000"
```

---

## 2. Globals

```yaml
globals:
  # Etat de verrouillage de l'ecran
  - id: display_lock
    type: bool
    initial_value: 'false'

  # Timestamp de debut du timeout sur face_unlock_page
  - id: unlock_timeout_start
    type: uint32_t
    initial_value: '0'

  # Duree du timeout sur face_unlock_page (en secondes)
  - id: face_unlock_timeout_sec
    type: int
    initial_value: '45'
```

---

## 3. Switch (controle camera)

```yaml
switch:
  - platform: template
    name: "LVGL Camera Display"
    id: lvgl_display_enable_switch
    restore_mode: RESTORE_DEFAULT_OFF
    optimistic: true
    turn_on_action:
      - lambda: |-
          auto *lvgl_disp = id(camera_display);
          if (lvgl_disp != nullptr) {
            lvgl_disp->set_enabled(true);
          }
    turn_off_action:
      - lambda: |-
          auto *lvgl_disp = id(camera_display);
          if (lvgl_disp != nullptr) {
            lvgl_disp->set_enabled(false);
          }
```

---

## 4. LVGL Camera Display

```yaml
lvgl_camera_display:
  id: camera_display
  camera_id: tab5_cam
  canvas_id: camera_canvas
  pedestrian_detection: false
  face_detection: true
  face_recognition: true
  face_db_path: "/sdcard/faces.db"
  recognition_threshold: 0.7
```

---

## 5. Number

```yaml
number:
  # Timeout avant mise en veille
  - platform: template
    name: "Screen Off"
    id: auto_off
    optimistic: true
    restore_value: true
    min_value: -1
    max_value: 300
    step: 1
    unit_of_measurement: s
    icon: 'mdi:television-off'
    initial_value: 30
    on_value:
      then:
        - lvgl.label.update:
            id: current_timeout_value
            text: !lambda |-
              int timeout = id(auto_off).state;
              if (timeout == -1) return "OFF";
              return (std::to_string(timeout) + "s").c_str();

  # Timeout sur la page face_unlock (configurable depuis Home Assistant)
  - platform: template
    name: "Face Unlock Timeout"
    id: face_unlock_timeout_number
    icon: "mdi:timer-lock"
    unit_of_measurement: "s"
    min_value: 10
    max_value: 120
    step: 5
    initial_value: 45
    optimistic: true
    restore_value: true
    on_value:
      then:
        - lambda: |-
            id(face_unlock_timeout_sec) = (int)x;
            ESP_LOGI("lock", "Face unlock timeout: %d sec", (int)x);
```

### Label requis dans votre page settings (current_timeout_value)

Ce label doit exister dans votre page settings pour afficher la valeur actuelle :

```yaml
# Dans votre page settings
- label:
    id: current_timeout_value
    text: "30s"
    x: 285
    align: LEFT_MID
    width: 80
    text_font: roboto_32
    text_color: color_white
    text_align: CENTER
```

---

## 6. LVGL on_idle

```yaml
lvgl:
  byte_order: little_endian
  displays:
    - main_display
  touchscreens:
    - touch

  on_idle:
    - timeout: !lambda |-
        int timeout = id(auto_off).state;
        return (timeout == -1) ? 86400000 : (timeout * 1000);
      then:
        - if:
            condition:
              lambda: 'return id(auto_off).state > 0;'
            then:
              - lambda: |-
                  id(display_lock) = true;
                  ESP_LOGI("lock", "Verrouillage automatique apres inactivite");
              - lvgl.page.show: page_sleep
              - light.turn_off: backlight
```

---

## 7. LVGL Pages

### 7.1 Page Sleep (ecran noir)

```yaml
    - id: page_sleep
      bg_color: 0x000000
      on_load:
        - lambda: |-
            ESP_LOGI("lock", "Page veille - ecran verrouille");
            id(display_lock) = true;
        # AUTO-ACTIVER le switch pour la reconnaissance faciale
        - switch.turn_on: lvgl_display_enable_switch
        - lambda: |-
            // Pre-configurer le canvas pour face_unlock_page
            auto canvas = id(face_unlock_canvas);
            if (canvas != nullptr) {
              id(camera_display).configure_canvas(canvas);
            }
      widgets:
        - obj:
            width: 1024
            height: 600
            x: 0
            y: 0
            bg_color: 0x000000
            bg_opa: COVER
            border_opa: TRANSP
        - label:
            id: sleep_time_label
            text: "12:34"
            align: CENTER
            y: -50
            text_color: 0x333333
            text_font: roboto_48
        - label:
            text: "Touchez l ecran pour deverrouiller"
            align: CENTER
            y: 50
            text_color: 0x222222
            text_font: roboto_24
        - obj:
            id: sleep_touch_zone
            width: 1024
            height: 600
            x: 0
            y: 0
            bg_opa: 0%
            border_opa: TRANSP
            on_click:
              then:
                - lambda: ESP_LOGI("lock", "Touch detecte");
                - light.turn_on: backlight
                - lvgl.page.show: face_unlock_page
```

### 7.2 Face Unlock Page (camera + clavier)

```yaml
    - id: face_unlock_page
      bg_color: 0x0d1117

      on_load:
        - lambda: |-
            ESP_LOGI("lock", "Page deverrouillage chargee");
            // Toujours verrouiller quand on entre sur cette page
            id(display_lock) = true;
            // Reset le resultat precedent
            id(camera_display).reset_last_recognition();
            // Demarrer la camera
            id(tab5_cam).start_streaming();
            // Configurer le canvas
            auto canvas = id(face_unlock_canvas);
            if (canvas != nullptr) {
              id(camera_display).configure_canvas(canvas);
            }
            // Reset timeout
            id(unlock_timeout_start) = millis();

      widgets:
        # ===== HEADER =====
        - obj:
            x: 0
            y: 0
            width: 1024
            height: 50
            bg_color: 0x161b22
            bg_opa: COVER
            border_opa: TRANSP
            radius: 0
            widgets:
              - label:
                  text: "DEVERROUILLAGE"
                  x: 20
                  y: 10
                  text_color: 0xFFFFFF
                  text_font: roboto_32

        # ===== SECTION GAUCHE: CAMERA =====
        - obj:
            id: camera_section
            x: 10
            y: 55
            width: 660
            height: 535
            bg_color: 0x161b22
            bg_opa: COVER
            radius: 15
            border_width: 1
            border_color: 0x30363d
            widgets:
              - obj:
                  x: 0
                  y: 0
                  width: 660
                  height: 45
                  bg_color: 0x21262d
                  bg_opa: COVER
                  radius: 15
                  border_opa: TRANSP
                  widgets:
                    - label:
                        text: "Reconnaissance Faciale"
                        x: 15
                        y: 10
                        text_color: 0x58a6ff
                        text_font: roboto_24

              - canvas:
                  id: face_unlock_canvas
                  width: 640
                  height: 480
                  x: 10
                  y: 50
                  bg_color: 0x000000
                  radius: 8

              - label:
                  id: face_status_label
                  text: "Regardez la camera..."
                  x: 0
                  y: 505
                  width: 660
                  text_align: CENTER
                  text_color: 0x58a6ff
                  text_font: roboto_18

        # ===== SECTION DROITE: CLAVIER PIN =====
        - obj:
            id: keypad_section
            x: 680
            y: 55
            width: 334
            height: 535
            bg_color: 0x161b22
            bg_opa: COVER
            radius: 15
            border_width: 1
            border_color: 0x30363d
            widgets:
              - obj:
                  x: 0
                  y: 0
                  width: 334
                  height: 45
                  bg_color: 0x21262d
                  bg_opa: COVER
                  radius: 15
                  border_opa: TRANSP
                  widgets:
                    - label:
                        text: "Code PIN"
                        x: 15
                        y: 10
                        text_color: 0x58a6ff
                        text_font: roboto_24

              - obj:
                  x: 17
                  y: 55
                  width: 300
                  height: 60
                  bg_color: 0x0d1117
                  radius: 10
                  border_width: 2
                  border_color: 0x30363d
                  widgets:
                    - label:
                        id: unlock_code_display
                        text: "_ _ _ _"
                        align: CENTER
                        text_color: 0xFFFFFF
                        text_font: roboto_32

              - buttonmatrix:
                  id: unlock_keypad
                  x: 17
                  y: 125
                  width: 300
                  height: 400
                  pad_all: 5
                  bg_opa: TRANSP
                  border_opa: TRANSP
                  items:
                    bg_color: 0x21262d
                    border_color: 0x30363d
                    border_width: 1
                    radius: 12
                    text_font: roboto_32
                    text_color: 0xFFFFFF
                    pressed:
                      bg_color: 0x58a6ff
                      text_color: 0x0d1117
                  rows:
                    - buttons:
                        - text: "1"
                          control:
                            no_repeat: true
                        - text: "2"
                          control:
                            no_repeat: true
                        - text: "3"
                          control:
                            no_repeat: true
                    - buttons:
                        - text: "4"
                          control:
                            no_repeat: true
                        - text: "5"
                          control:
                            no_repeat: true
                        - text: "6"
                          control:
                            no_repeat: true
                    - buttons:
                        - text: "7"
                          control:
                            no_repeat: true
                        - text: "8"
                          control:
                            no_repeat: true
                        - text: "9"
                          control:
                            no_repeat: true
                    - buttons:
                        - text: "<"
                          key_code: "*"
                          control:
                            no_repeat: true
                        - text: "0"
                          control:
                            no_repeat: true
                        - text: "OK"
                          key_code: "#"
                          control:
                            no_repeat: true

        # ===== BARRE TIMEOUT =====
        - bar:
            id: unlock_timeout_bar
            x: 10
            y: 595
            width: 1004
            height: 5
            value: 100
            bg_color: 0x21262d
            radius: 3
            indicator:
              bg_color: 0x3fb950
              radius: 3
```

---

## 8. Key Collector

```yaml
key_collector:
  # Deverrouillage ecran
  - source_id: unlock_keypad
    min_length: 4
    max_length: 4
    end_keys: "#"
    end_key_required: true
    back_keys: "*"
    allowed_keys: "0123456789*#"
    timeout: 10s
    on_progress:
      - if:
          condition:
            lambda: return (0 != x.compare(std::string{""}));
          then:
            - lvgl.label.update:
                id: unlock_code_display
                text: !lambda |-
                  std::string display = "";
                  for (size_t i = 0; i < x.length(); i++) {
                    display += "* ";
                  }
                  for (size_t i = x.length(); i < 4; i++) {
                    display += "_ ";
                  }
                  return display.c_str();
          else:
            - lvgl.label.update:
                id: unlock_code_display
                text_color: 0xFFFFFF
                text: "_ _ _ _"
    on_result:
      - if:
          condition:
            lambda: 'return x == "${unlock_pin_code}";'
          then:
            # Code correct
            - lvgl.label.update:
                id: unlock_code_display
                text_color: 0x3fb950
                text: "O K"
            - lambda: |-
                ESP_LOGI("unlock", "Code ecran correct - deverrouillage!");
                id(display_lock) = false;
                id(tab5_cam).stop_streaming();
                auto canvas = id(camera_canvas);
                if (canvas != nullptr) {
                  id(camera_display).configure_canvas(canvas);
                }
            # DESACTIVER le switch apres PIN correct
            - switch.turn_off: lvgl_display_enable_switch
            - delay: 500ms
            - lvgl.page.show: page_home
          else:
            # Code incorrect
            - lvgl.label.update:
                id: unlock_code_display
                text_color: 0xf85149
                text: "ERREUR"
            - lambda: ESP_LOGW("unlock", "Code ecran incorrect!");
            - delay: 1000ms
            - lvgl.label.update:
                id: unlock_code_display
                text_color: 0xFFFFFF
                text: "_ _ _ _"
```

---

## 9. Interval (reconnaissance faciale + timeout)

```yaml
interval:
  - interval: 500ms
    then:
      - lambda: |-
          lv_obj_t *current = lv_scr_act();
          if (current != id(face_unlock_page)->obj) return;

          // Timeout
          uint32_t elapsed = (millis() - id(unlock_timeout_start)) / 1000;
          int remaining = id(face_unlock_timeout_sec) - elapsed;
          int percent = (remaining * 100) / id(face_unlock_timeout_sec);
          if (percent < 0) percent = 0;
          lv_bar_set_value(id(unlock_timeout_bar), percent, LV_ANIM_ON);

          if (remaining <= 5) {
            lv_obj_set_style_bg_color(id(unlock_timeout_bar), lv_color_hex(0xf85149), LV_PART_INDICATOR);
          } else {
            lv_obj_set_style_bg_color(id(unlock_timeout_bar), lv_color_hex(0x3fb950), LV_PART_INDICATOR);
          }

          // Timeout expire
          if (remaining <= 0) {
            ESP_LOGI("lock", "Timeout - retour veille");
            id(tab5_cam).stop_streaming();
            auto canvas = id(camera_canvas);
            if (canvas) id(camera_display).configure_canvas(canvas);
          }

          // Verifier visage detecte
          int face_count = id(camera_display).get_detected_face_count();
          if (face_count == 0) {
            lv_label_set_text(id(face_status_label), "Aucun visage detecte...");
            lv_obj_set_style_text_color(id(face_status_label), lv_color_hex(0x8b949e), 0);
            return;
          }

          lv_label_set_text(id(face_status_label), "Visage detecte - Verification...");
          lv_obj_set_style_text_color(id(face_status_label), lv_color_hex(0x58a6ff), 0);

          // Verifier reconnaissance
          auto result = id(camera_display).get_last_recognition();
          if (result.recognized && result.similarity >= 0.70f) {
            ESP_LOGI("lock", "VISAGE RECONNU! ID=%d sim=%.2f", result.id, result.similarity);
            id(display_lock) = false;
            lv_label_set_text(id(face_status_label), "Bienvenue!");
            lv_obj_set_style_text_color(id(face_status_label), lv_color_hex(0x3fb950), 0);

            id(tab5_cam).stop_streaming();
            auto canvas = id(camera_canvas);
            if (canvas) id(camera_display).configure_canvas(canvas);
          }

      # Transition apres reconnaissance
      - if:
          condition:
            lambda: |-
              lv_obj_t *current = lv_scr_act();
              return (current == id(face_unlock_page)->obj) && !id(display_lock);
          then:
            # DESACTIVER le switch apres reconnaissance faciale
            - switch.turn_off: lvgl_display_enable_switch
            - delay: 500ms
            - lvgl.page.show: page_home

      # Retour veille si timeout
      - if:
          condition:
            lambda: |-
              lv_obj_t *current = lv_scr_act();
              if (current != id(face_unlock_page)->obj) return false;
              uint32_t elapsed = (millis() - id(unlock_timeout_start)) / 1000;
              return elapsed >= id(face_unlock_timeout_sec);
          then:
            - light.turn_off: backlight
            - lvgl.page.show: page_sleep
```

---

## 10. Camera Page (utilisation manuelle)

Pour votre page camera, le switch peut etre active manuellement ou automatiquement :

```yaml
      - id: camera_page
        bg_color: 0x000000
        on_load:
          - lambda: |-
              ESP_LOGI("camera", "Page camera chargee");
              // Configurer canvas pour cette page
              auto canvas = id(camera_canvas);
              if (canvas != nullptr) {
                id(camera_display).configure_canvas(canvas);
              }
          # Optionnel: activer automatiquement
          # - switch.turn_on: lvgl_display_enable_switch
          # - lambda: id(tab5_cam).start_streaming();

        on_unload:
          - lambda: |-
              ESP_LOGI("camera", "Quitte page camera");
              id(tab5_cam).stop_streaming();
          - switch.turn_off: lvgl_display_enable_switch
```

**Note:** Sur camera_page, l'utilisateur peut controler le switch manuellement via les boutons START/STOP.

---

## 11. Resume des valeurs

| Parametre | Valeur par defaut | Description |
|-----------|-------------------|-------------|
| `unlock_pin_code` | "0000" | Code PIN pour deverrouiller |
| `auto_off` | 30s | Temps avant mise en veille |
| `face_unlock_timeout_sec` | 45s | Temps sur la page face_unlock |
| Seuil reconnaissance | 0.70 | Similarite minimum (70%) |

---

## 12. Fonctions C++ disponibles

| Fonction | Description |
|----------|-------------|
| `reset_last_recognition()` | Reset le resultat de reconnaissance |
| `get_detected_face_count()` | Nombre de visages detectes |
| `get_last_recognition()` | Dernier resultat de reconnaissance |
| `enroll_face()` | Enregistrer un nouveau visage |
| `get_enrolled_count()` | Nombre de visages enregistres |
| `clear_all_faces()` | Supprimer tous les visages |

---

## 13. Flux du switch camera

```
page_home
     |
     | [idle timeout]
     v
page_sleep -----> switch.turn_on (auto)
     |            configure_canvas(face_unlock_canvas)
     | [touch]
     v
face_unlock_page
     |
     +--- [face reconnu] ---> switch.turn_off --> page_home
     |
     +--- [PIN correct] ----> switch.turn_off --> page_home
     |
     +--- [timeout] --------> page_sleep (switch reste ON)


camera_page (manuel)
     |
     | [btn START] --> switch.turn_on + start_streaming
     |
     | [btn STOP]  --> switch.turn_off + stop_streaming
     |
     | [on_unload] --> switch.turn_off + stop_streaming
```
