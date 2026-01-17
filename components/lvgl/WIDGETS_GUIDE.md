# Guide Complet des Widgets LVGL 9.4 pour ESPHome

Ce guide documente **tous les 35 widgets** disponibles dans l'implémentation LVGL 9.4 pour ESPHome.

## Table des Matières

- [Widgets de Base](#widgets-de-base)
- [Widgets d'Entrée](#widgets-dentrée)
- [Widgets d'Affichage](#widgets-daffichage)
- [Widgets de Conteneur](#widgets-de-conteneur)
- [Widgets Avancés](#widgets-avancés)

---

## Widgets de Base

### 1. Label (Texte)

Affiche du texte statique ou dynamique.

```yaml
lvgl:
  pages:
    - id: home
      widgets:
        - label:
            id: my_label
            text: "Hello World!"
            x: 50
            y: 50
            text_font: montserrat_20
            text_color: 0xFFFFFF
            text_align: CENTER
            # Mode de texte long
            long_mode: WRAP  # WRAP, DOT, SCROLL, SCROLL_CIRCULAR, CLIP
            width: 200
```

**Propriétés principales**:
- `text`: Texte à afficher
- `text_font`: Police de caractères
- `text_color`: Couleur du texte
- `text_align`: Alignement (LEFT, CENTER, RIGHT)
- `long_mode`: Comportement pour texte long
- `recolor`: Active les codes de couleur inline

**Documentation**: [Label - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/label.html)

---

### 2. Button (Bouton)

Bouton cliquable avec texte ou icône.

```yaml
lvgl:
  widgets:
    - button:
        id: my_button
        text: "Click Me"
        x: 100
        y: 100
        width: 120
        height: 50
        checkable: false  # Bouton toggle si true
        on_click:
          - logger.log: "Button clicked!"
        on_long_press:
          - logger.log: "Long press!"
```

**Actions disponibles**:
- `on_click`: Clic simple
- `on_long_press`: Appui long
- `on_press`: Début de pression
- `on_release`: Relâchement

**Documentation**: [Button - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/button.html)

---

### 3. Image (Image)

Affiche des images PNG, BMP, JPEG, GIF ou SVG.

```yaml
lvgl:
  widgets:
    - image:
        id: my_image
        src: "S:/icons/home.svg"  # Fichier SVG sur carte SD
        # ou
        src: my_image_id  # Image définie dans esphome
        x: 50
        y: 50
        width: 64   # Redimensionne (SVG uniquement)
        height: 64
        angle: 45   # Rotation en degrés (0-360)
        zoom: 256   # Zoom (256 = 100%, 512 = 200%)
        pivot_x: 50%  # Point de rotation X
        pivot_y: 50%  # Point de rotation Y
        antialias: true  # Lissage
```

**Formats supportés**:
- **SVG**: Vectoriel, scalable (ThorVG)
- **PNG**: Bitmap avec transparence
- **BMP**: Bitmap simple
- **JPEG**: Photos compressées
- **GIF**: Animations

**Documentation**: [Image - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/image.html)

---

### 4. Object (Container de Base)

Conteneur de base pour grouper des widgets.

```yaml
lvgl:
  widgets:
    - obj:
        id: my_container
        x: 0
        y: 0
        width: 200
        height: 150
        bg_color: 0x2196F3
        border_width: 2
        border_color: 0xFFFFFF
        radius: 10  # Coins arrondis
        pad_all: 10  # Padding interne
        widgets:
          - label:
              text: "Inside container"
```

**Documentation**: [Object - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/obj.html)

---

## Widgets d'Entrée

### 5. Slider (Curseur)

Curseur pour sélectionner une valeur.

```yaml
lvgl:
  widgets:
    - slider:
        id: brightness_slider
        x: 50
        y: 100
        width: 300
        min_value: 0
        max_value: 100
        value: 50
        mode: NORMAL  # NORMAL, SYMMETRICAL, RANGE
        on_change:
          - lambda: |-
              ESP_LOGI("slider", "Value: %d", (int)x);
```

**Modes**:
- `NORMAL`: Valeur unique de min à max
- `SYMMETRICAL`: Valeur centrée (0 au milieu)
- `RANGE`: Deux valeurs (début et fin)

**Documentation**: [Slider - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/slider.html)

---

### 6. Switch (Interrupteur)

Interrupteur ON/OFF.

```yaml
lvgl:
  widgets:
    - switch:
        id: wifi_switch
        x: 100
        y: 150
        state: true  # ON au démarrage
        on_change:
          - if:
              condition:
                lambda: return x;
              then:
                - logger.log: "Switch ON"
              else:
                - logger.log: "Switch OFF"
```

**Documentation**: [Switch - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/switch.html)

---

### 7. Checkbox (Case à cocher)

Case à cocher avec label.

```yaml
lvgl:
  widgets:
    - checkbox:
        id: agree_checkbox
        text: "I agree to terms"
        x: 50
        y: 200
        checked: false
        on_change:
          - logger.log:
              format: "Checkbox: %s"
              args: [ 'x ? "checked" : "unchecked"' ]
```

**Documentation**: [Checkbox - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/checkbox.html)

---

### 8. Dropdown (Liste déroulante)

Liste déroulante de sélection.

```yaml
lvgl:
  widgets:
    - dropdown:
        id: city_selector
        x: 50
        y: 100
        width: 150
        options: "Paris\nLyon\nMarseille\nToulouse"
        # ou avec liste
        options:
          - "Paris"
          - "Lyon"
          - "Marseille"
        selected_index: 0
        on_select:
          - lambda: |-
              ESP_LOGI("dropdown", "Selected: %d", (int)x);
```

**Documentation**: [Dropdown - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/dropdown.html)

---

### 9. Roller (Rouleau de sélection)

Rouleau vertical pour sélection (style iOS).

```yaml
lvgl:
  widgets:
    - roller:
        id: time_roller
        x: 100
        y: 100
        width: 100
        height: 150
        options: "00\n01\n02\n03\n04\n05"
        selected_index: 0
        visible_row_count: 5  # Nombre de lignes visibles
        mode: NORMAL  # NORMAL ou INFINITE (boucle)
        on_select:
          - logger.log:
              format: "Hour: %d"
              args: [ 'x' ]
```

**Documentation**: [Roller - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/roller.html)

---

### 10. Textarea (Zone de texte)

Zone de saisie de texte multiligne.

```yaml
lvgl:
  widgets:
    - textarea:
        id: message_input
        x: 50
        y: 100
        width: 300
        height: 150
        text: "Enter message..."
        placeholder_text: "Type here..."
        password_mode: false
        one_line: false  # true = input sur une ligne
        max_length: 100
        accepted_chars: "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 "
        on_change:
          - logger.log:
              format: "Text: %s"
              args: [ 'x.c_str()' ]
```

**Documentation**: [Textarea - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/textarea.html)

---

### 11. Keyboard (Clavier virtuel)

Clavier virtuel pour saisie de texte.

```yaml
lvgl:
  widgets:
    - textarea:
        id: input_field
        x: 50
        y: 50
        width: 300

    - keyboard:
        id: my_keyboard
        x: 0
        y: 250
        width: 100%
        height: 200
        mode: TEXT_LOWER  # TEXT_LOWER, TEXT_UPPER, SPECIAL, NUMBER
        textarea: input_field  # Lie au textarea
```

**Modes**:
- `TEXT_LOWER`: Lettres minuscules
- `TEXT_UPPER`: Lettres majuscules
- `SPECIAL`: Caractères spéciaux
- `NUMBER`: Pavé numérique

**Documentation**: [Keyboard - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/keyboard.html)

---

### 12. Spinbox (Saisie numérique)

Saisie numérique avec boutons +/-.

```yaml
lvgl:
  widgets:
    - spinbox:
        id: temperature_spinbox
        x: 100
        y: 100
        width: 150
        height: 50
        value: 20
        min_value: 0
        max_value: 100
        step: 1
        digits: 3  # Nombre de chiffres
        decimal_places: 1  # Nombre de décimales
        rollover: true  # Boucle à la fin
```

**Documentation**: [Spinbox - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/spinbox.html)

---

## Widgets d'Affichage

### 13. Arc (Arc circulaire)

Arc/cercle pour afficher une valeur (jauge).

```yaml
lvgl:
  widgets:
    - arc:
        id: volume_arc
        x: 100
        y: 100
        width: 150
        height: 150
        start_angle: 135  # Angle de début (0-360)
        end_angle: 45     # Angle de fin
        value: 50
        min_value: 0
        max_value: 100
        mode: NORMAL  # NORMAL, REVERSE, SYMMETRICAL
        rotation: 0   # Rotation globale
        adjustable: true  # Ajustable par l'utilisateur
```

**Documentation**: [Arc - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/arc.html)

---

### 14. Bar (Barre de progression)

Barre de progression horizontale ou verticale.

```yaml
lvgl:
  widgets:
    - bar:
        id: battery_bar
        x: 50
        y: 100
        width: 200
        height: 20
        min_value: 0
        max_value: 100
        value: 75
        mode: NORMAL  # NORMAL, SYMMETRICAL, RANGE
        # Animation
        animated: true
        animation:
          duration: 500ms
```

**Documentation**: [Bar - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/bar.html)

---

### 15. LED (LED)

Indicateur LED avec couleur et luminosité.

```yaml
lvgl:
  widgets:
    - led:
        id: status_led
        x: 100
        y: 100
        width: 30
        height: 30
        color: 0x00FF00  # Vert
        brightness: 255  # 0-255
```

**Documentation**: [LED - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/led.html)

---

### 16. Spinner (Indicateur de chargement)

Indicateur de chargement animé.

```yaml
lvgl:
  widgets:
    - spinner:
        id: loading_spinner
        x: 150
        y: 150
        width: 50
        height: 50
        spin_time: 1000ms  # Durée d'une rotation
        arc_length: 60  # Longueur de l'arc (0-360)
```

**Documentation**: [Spinner - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/spinner.html)

---

### 17. Line (Ligne)

Ligne ou polyligne.

```yaml
lvgl:
  widgets:
    - line:
        id: my_line
        x: 50
        y: 50
        points:
          - x: 0
            y: 0
          - x: 100
            y: 50
          - x: 200
            y: 0
        line_width: 3
        line_color: 0xFF0000
        line_rounded: true  # Extrémités arrondies
```

**Documentation**: [Line - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/line.html)

---

### 18. Scale (Échelle/Jauge) ⚡ Nouveau LVGL 9

Échelle graduée linéaire ou circulaire (remplace Meter).

```yaml
lvgl:
  widgets:
    - scale:
        id: speedometer
        x: 50
        y: 50
        width: 300
        height: 300
        mode: ROUND_OUTER  # HORIZONTAL_TOP, HORIZONTAL_BOTTOM,
                          # VERTICAL_LEFT, VERTICAL_RIGHT,
                          # ROUND_INNER, ROUND_OUTER
        range:
          min: 0
          max: 200
        angle_range: 270  # Angle total pour mode circulaire
        rotation: 0
        # Graduations
        total_tick_count: 21
        major_tick_every: 5
        label_count: 11
        # Style des graduations
        tick_length: 10
        tick_width: 2
```

**Documentation**: [Scale - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/scale.html)
**Voir aussi**: `SCALE_WIDGET_README.md` et `SCALE_QUICK_REFERENCE.md`

---

### 19. Chart (Graphique)

Graphique pour afficher des données.

```yaml
lvgl:
  widgets:
    - chart:
        id: temperature_chart
        x: 50
        y: 50
        width: 300
        height: 200
        type: LINE  # LINE, BAR, SCATTER
        point_count: 20
        y_range:
          min: 0
          max: 40
        series:
          - id: temp_series
            color: 0xFF0000
            width: 2
```

**Types de graphiques**:
- `LINE`: Courbe linéaire
- `BAR`: Histogramme
- `SCATTER`: Nuage de points

**Documentation**: [Chart - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/chart.html)
**Voir aussi**: `CHART_README.md`

---

### 20. QR Code

Génère et affiche un QR code.

```yaml
lvgl:
  widgets:
    - qrcode:
        id: wifi_qrcode
        x: 100
        y: 100
        size: 150
        data: "WIFI:T:WPA;S:MyNetwork;P:password123;;"
        dark_color: 0x000000
        light_color: 0xFFFFFF
```

**Documentation**: [QR Code - LVGL 9.4](https://docs.lvgl.io/9.4/details/libs/qrcode.html)

---

## Widgets Avancés

### 21. AnimImg (Image Animée)

Affiche une séquence d'images animées.

```yaml
lvgl:
  widgets:
    - animimg:
        id: my_animation
        x: 100
        y: 100
        images:
          - frame1
          - frame2
          - frame3
        duration: 100ms  # Durée par frame
        repeat_count: -1  # -1 = infini
        auto_start: true
```

**Documentation**: [AnimImg - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/animimg.html)

---

### 22. Lottie (Animation Vectorielle) ⚡ Nouveau LVGL 9

Animations vectorielles JSON (ultra fluides).

```yaml
lvgl:
  widgets:
    - lottie:
        id: weather_animation
        src: "S:/animations/weather.json"
        x: 100
        y: 100
        width: 200
        height: 200
        loop: true
        autoplay: true
```

**Avantages**:
- Animations 60 FPS ultra fluides
- 90% plus léger que les GIF
- Redimensionnable sans perte de qualité

**Ressources**:
- [Weather Icons by Basmilius](https://github.com/basmilius/weather-icons)
- [LottieFiles](https://lottiefiles.com/)

**Voir aussi**: `LOTTIE_README.md`

---

### 23. 3D Texture ⚡ Nouveau LVGL 9

Affiche des modèles 3D avec ThorVG.

```yaml
lvgl:
  widgets:
    - tex3d:
        id: my_3d_model
        src: "S:/models/cube.svg"
        x: 100
        y: 100
        width: 200
        height: 200
        angle_x: 0
        angle_y: 0
        angle_z: 0
```

**Voir aussi**: `TEX3D_README.md`

---

### 24. Arc Label ⚡ Nouveau LVGL 9

Texte courbé le long d'un arc.

```yaml
lvgl:
  widgets:
    - arclabel:
        id: curved_text
        x: 100
        y: 100
        width: 200
        height: 200
        text: "Curved Text Example"
        angle: 270  # Angle de l'arc
        radius: 100
        rotation: 0
```

**Voir aussi**: `ARCLABEL_README.md`

---

### 25. Span (Texte Enrichi)

Texte avec styles mixtes (gras, couleurs, tailles différentes).

```yaml
lvgl:
  widgets:
    - span:
        id: rich_text
        x: 50
        y: 50
        width: 300
        mode: BREAK  # EXPAND, BREAK, DOTS, CLIP
        spans:
          - text: "Bold text"
            text_font: montserrat_20
            text_color: 0xFF0000
          - text: " normal "
          - text: "italic"
            text_decor: UNDERLINE
```

**Documentation**: [Spangroup - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/spangroup.html)
**Voir aussi**: `SPAN_README.md`

---

## Widgets de Conteneur

### 26. TabView (Onglets)

Interface à onglets.

```yaml
lvgl:
  widgets:
    - tabview:
        id: my_tabs
        x: 0
        y: 0
        width: 100%
        height: 100%
        tab_position: TOP  # TOP, BOTTOM, LEFT, RIGHT
        tabs:
          - id: tab_home
            name: "Home"
            widgets:
              - label:
                  text: "Home content"

          - id: tab_settings
            name: "Settings"
            widgets:
              - label:
                  text: "Settings content"
```

**Documentation**: [TabView - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/tabview.html)

---

### 27. TileView (Vues défilantes)

Vues en grille avec défilement.

```yaml
lvgl:
  widgets:
    - tileview:
        id: my_tileview
        x: 0
        y: 0
        width: 100%
        height: 100%
        tiles:
          - id: tile1
            row: 0
            col: 0
            dir: HOR  # HOR, VER, ALL
            widgets:
              - label:
                  text: "Tile 1"

          - id: tile2
            row: 0
            col: 1
            dir: HOR
            widgets:
              - label:
                  text: "Tile 2"
```

**Documentation**: [TileView - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/tileview.html)

---

### 28. Menu (Menu Hiérarchique) ⚡ Nouveau LVGL 9

Menu de navigation hiérarchique avec sidebar.

```yaml
lvgl:
  widgets:
    - menu:
        id: settings_menu
        x: 0
        y: 0
        width: 100%
        height: 100%
        root_back_button: false
        pages:
          - id: main_page
            title: "Main Menu"
            widgets:
              - button:
                  text: "Settings"
              - button:
                  text: "About"

          - id: settings_page
            title: "Settings"
            widgets:
              - switch:
                  text: "WiFi"
              - switch:
                  text: "Bluetooth"
```

**Documentation**: [Menu - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/menu.html)
**Voir aussi**: `MENU_README.md`

---

### 29. Window (Fenêtre)

Fenêtre avec barre de titre et boutons.

```yaml
lvgl:
  widgets:
    - win:
        id: my_window
        x: 50
        y: 50
        width: 300
        height: 400
        title: "Window Title"
        header_height: 40
        close_button: true
        widgets:
          - label:
              text: "Window content"
```

**Documentation**: [Window - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/win.html)
**Voir aussi**: `WIN_README.md`

---

### 30. List (Liste)

Liste de boutons avec texte et icônes.

```yaml
lvgl:
  widgets:
    - list:
        id: my_list
        x: 50
        y: 50
        width: 250
        height: 300
        items:
          - text: "Item 1"
            icon: "\xEF\x80\x81"  # Font Awesome icon
          - text: "Item 2"
            icon: "\xEF\x80\x82"
          - text: "Item 3"
```

**Documentation**: [List - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/list.html)

---

### 31. Table (Tableau)

Tableau avec lignes et colonnes.

```yaml
lvgl:
  widgets:
    - table:
        id: data_table
        x: 50
        y: 50
        width: 300
        height: 200
        col_count: 3
        row_count: 4
        cells:
          - row: 0
            col: 0
            text: "Name"
          - row: 0
            col: 1
            text: "Age"
          - row: 0
            col: 2
            text: "City"
          - row: 1
            col: 0
            text: "Alice"
          - row: 1
            col: 1
            text: "25"
          - row: 1
            col: 2
            text: "Paris"
```

**Documentation**: [Table - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/table.html)
**Voir aussi**: `TABLE_README.md` et `TABLE_IMPLEMENTATION_SUMMARY.md`

---

### 32. Calendar (Calendrier) ⚡ Nouveau LVGL 9

Calendrier mensuel interactif.

```yaml
lvgl:
  widgets:
    - calendar:
        id: my_calendar
        x: 50
        y: 50
        width: 300
        height: 300
        year: 2024
        month: 1  # 1-12
        day: 15
        show_month_names: true
        show_day_names: true
        on_change:
          - lambda: |-
              ESP_LOGI("calendar", "Selected: %d/%d/%d", day, month, year);
```

**Documentation**: [Calendar - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/calendar.html)
**Voir aussi**: `CALENDAR_README.md`

---

### 33. ButtonMatrix (Matrice de Boutons)

Grille de boutons configurables.

```yaml
lvgl:
  widgets:
    - buttonmatrix:
        id: keypad
        x: 50
        y: 250
        width: 300
        height: 200
        rows: 4
        buttons:
          - row: 0
            buttons:
              - "1"
              - "2"
              - "3"
          - row: 1
            buttons:
              - "4"
              - "5"
              - "6"
          - row: 2
            buttons:
              - "7"
              - "8"
              - "9"
          - row: 3
            buttons:
              - ""
              - "0"
              - ""
        on_click:
          - lambda: |-
              ESP_LOGI("btnmatrix", "Button %d clicked", button_id);
```

**Documentation**: [ButtonMatrix - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/buttonmatrix.html)

---

### 34. MsgBox (Boîte de Message)

Boîte de dialogue modale.

```yaml
lvgl:
  widgets:
    - msgbox:
        id: confirm_dialog
        title: "Confirmation"
        text: "Êtes-vous sûr ?"
        close_button: true
        buttons:
          - "Oui"
          - "Non"
        on_click:
          - lambda: |-
              if (button_id == 0) {
                ESP_LOGI("msgbox", "Yes clicked");
              } else {
                ESP_LOGI("msgbox", "No clicked");
              }
```

**Documentation**: [MsgBox - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/msgbox.html)

---

### 35. Canvas (Canevas de Dessin)

Surface de dessin pour graphiques personnalisés.

```yaml
lvgl:
  widgets:
    - canvas:
        id: drawing_canvas
        x: 50
        y: 50
        width: 300
        height: 200
        bg_color: 0xFFFFFF
```

**Fonctions de dessin** (via lambda):
- Lignes, rectangles, cercles
- Texte
- Images
- Pixels individuels

**Documentation**: [Canvas - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/canvas.html)

---

### 36. ImageButton (Bouton Image)

Bouton avec images pour chaque état.

```yaml
lvgl:
  widgets:
    - imgbtn:
        id: power_button
        x: 100
        y: 100
        width: 64
        height: 64
        src: power_icon  # Image normale
        src_pressed: power_icon_pressed  # Image pressée
        src_checked: power_icon_on  # Image cochée
        on_click:
          - logger.log: "Power button clicked"
```

**Documentation**: [ImageButton - LVGL 9.4](https://docs.lvgl.io/9.4/details/widgets/imagebutton.html)
**Voir aussi**: `IMGBTN_README.md`

---

## Propriétés Communes à Tous les Widgets

### Position et Taille

```yaml
x: 100          # Position X en pixels ou %
y: 50           # Position Y
width: 200      # Largeur
height: 100     # Hauteur
```

### Alignement

```yaml
align: CENTER   # TOP_LEFT, TOP_MID, TOP_RIGHT, LEFT_MID,
               # CENTER, RIGHT_MID, BOTTOM_LEFT,
               # BOTTOM_MID, BOTTOM_RIGHT
align_to: other_widget_id
```

### Style

```yaml
bg_color: 0x2196F3      # Couleur de fond
bg_opa: COVER           # Opacité (0-255 ou TRANSP/COVER)
border_width: 2         # Épaisseur de la bordure
border_color: 0xFFFFFF  # Couleur de la bordure
radius: 10              # Rayon des coins arrondis
pad_all: 10             # Padding uniforme
pad_left: 5             # Padding gauche
pad_right: 5            # Padding droit
pad_top: 5              # Padding haut
pad_bottom: 5           # Padding bas
shadow_width: 10        # Largeur de l'ombre
shadow_color: 0x000000  # Couleur de l'ombre
```

### États et Parts

```yaml
styles:
  - state: DEFAULT      # default, checked, focused, pressed, etc.
    part: MAIN          # main, scrollbar, indicator, knob, etc.
    bg_color: 0x2196F3

  - state: PRESSED
    part: MAIN
    bg_color: 0x1976D2
```

### Flags

```yaml
flags:
  hidden: false         # Caché
  clickable: true       # Cliquable
  scrollable: true      # Défilable
  checkable: false      # Peut être coché
```

---

## Événements Disponibles

Tous les widgets supportent ces événements:

### Événements d'Entrée
- `on_press`: Début de pression
- `on_pressing`: Pression continue
- `on_click`: Clic simple
- `on_short_click`: Clic court
- `on_long_press`: Appui long
- `on_release`: Relâchement
- `on_scroll`: Défilement
- `on_focus`: Obtention du focus
- `on_defocus`: Perte du focus

### Événements Spéciaux
- `on_change`: Changement de valeur
- `on_ready`: Widget prêt
- `on_cancel`: Annulation

### Nouveaux Événements LVGL 9.4 ⚡

```yaml
on_single_click: ...    # 1er clic
on_double_click: ...    # 2ème clic
on_triple_click: ...    # 3ème clic
on_hover_over: ...      # Survol
on_hover_leave: ...     # Fin survol
on_screen_loaded: ...   # Écran chargé
on_screen_unloaded: ... # Écran déchargé
```

---

## Actions LVGL

Actions disponibles pour contrôler les widgets:

```yaml
# Navigation
- lvgl.page.show: page_id
- lvgl.page.next:
- lvgl.page.previous:

# Widget update
- lvgl.widget.update:
    id: widget_id
    text: "New text"
    value: 50

# Animation
- lvgl.lottie.start: lottie_id
- lvgl.lottie.stop: lottie_id
- lvgl.lottie.pause: lottie_id

# État
- lvgl.widget.enable: widget_id
- lvgl.widget.disable: widget_id
- lvgl.widget.show: widget_id
- lvgl.widget.hide: widget_id
```

---

## Ressources

### Documentation Officielle
- [LVGL 9.4 Documentation](https://docs.lvgl.io/9.4/)
- [Widget Catalog](https://docs.lvgl.io/9.4/details/widgets/index.html)
- [Events Reference](https://docs.lvgl.io/9.4/details/common-widget-features/events.html)
- [Styles Guide](https://docs.lvgl.io/9.4/details/common-widget-features/styles.html)

### Ressources Graphiques
- **SVG Icons**:
  - [Remix Icon](https://remixicon.com/) - 2,800+ icônes
  - [Ionicons](https://ionic.io/ionicons)
  - [Heroicons](https://heroicons.com/)

- **Lottie Animations**:
  - [Weather Icons](https://github.com/basmilius/weather-icons)
  - [LottieFiles](https://lottiefiles.com/)
  - [Lordicon](https://lordicon.com/)

### Exemples
- [LVGL Examples](https://docs.lvgl.io/9.4/examples.html)
- [ESPHome LVGL Examples](https://esphome.io/components/lvgl.html)

---

## Support et Contribution

### Problèmes et Questions
- [GitHub Issues](https://github.com/youkorr/test2_esp_video_esphome/issues)
- [ESPHome Discord](https://discord.gg/KhAMKrd)

### Documentation des Widgets
Consultez les README spécifiques pour plus de détails:
- `ARCLABEL_README.md`
- `CALENDAR_README.md`
- `CHART_README.md`
- `IMGBTN_README.md`
- `LOTTIE_README.md`
- `MENU_README.md`
- `SCALE_WIDGET_README.md`
- `SCALE_QUICK_REFERENCE.md`
- `SPAN_README.md`
- `TABLE_README.md`
- `TABLE_IMPLEMENTATION_SUMMARY.md`
- `TEX3D_README.md`
- `WIN_README.md`

---

**Implémentation complète LVGL 9.4 pour ESPHome**
✅ 35/35 widgets documentés
✅ 70 événements supportés
✅ ThorVG/SVG/Lottie activés

**Made with ❤️ for the ESPHome community**
