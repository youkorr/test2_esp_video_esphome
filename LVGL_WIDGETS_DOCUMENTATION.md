# LVGL Widgets Documentation

Complete documentation for all LVGL v9.4 widgets in ESPHome.

---

## Table of Contents

### New Widgets (LVGL v9.4)
- [Arc Label](#arc-label-lv_arclabel)
- [Calendar](#calendar-lv_calendar)
- [Chart](#chart-lv_chart)
- [Image Button](#image-button-lv_imagebutton)
- [List](#list-lv_list)
- [Lottie](#lottie-lv_lottie)
- [Menu](#menu-lv_menu)
- [Scale](#scale-lv_scale)
- [Spangroup](#spangroup-lv_spangroup)
- [Table](#table-lv_table)
- [3D Texture](#3d-texture-lv_3dtexture)
- [Window](#window-lv_win)

---

## Arc Label (lv_arclabel)

Display text along a curved path. Useful for circular displays and decorative text.

### Configuration Variables

- **text** (**Required**, string): The text to display along the arc.
- **radius** (*Optional*, int): Radius of the arc path in pixels. Defaults to `100`.
- **start_angle** (*Optional*, int): Starting angle in degrees (0-360). Defaults to `0`.
- **end_angle** (*Optional*, int): Ending angle in degrees (0-360). Defaults to `360`.
- **rotation** (*Optional*, int): Rotation offset in degrees. Defaults to `0`.

All [common widget properties](#common-widget-properties) are supported.

### Example

```yaml
- arclabel:
    id: curved_text
    text: "Circular Display"
    x: 120
    y: 120
    radius: 100
    start_angle: 45
    end_angle: 315
    text_color: 0xFF0000
    text_font: roboto_16
```

---

## Calendar (lv_calendar)

Interactive calendar widget for date selection with year, month, and day navigation.

### Configuration Variables

- **today_date** (*Optional*, dict): Initial date to show as "today"
  - **year** (**Required**, int): Year (e.g., 2024)
  - **month** (**Required**, int): Month (1-12)
  - **day** (**Required**, int): Day (1-31)
- **showed_date** (*Optional*, dict): Initially displayed month/year
  - **year** (**Required**, int): Year
  - **month** (**Required**, int): Month (1-12)
- **highlighted_dates** (*Optional*, list): List of dates to highlight
  - **year** (**Required**, int): Year
  - **month** (**Required**, int): Month
  - **day** (**Required**, int): Day

All [common widget properties](#common-widget-properties) are supported.

### Widget Parts

- **main**: Calendar background
- **items**: Individual date cells
- **header**: Month/year header area

### Triggers

- **on_value**: Triggered when a date is selected. The selected date is available in the lambda.

### Actions

- `lvgl.calendar.set_today_date`: Set the "today" date
  - **year** (**Required**, int): Year
  - **month** (**Required**, int): Month (1-12)
  - **day** (**Required**, int): Day (1-31)

- `lvgl.calendar.set_showed_date`: Change displayed month/year
  - **year** (**Required**, int): Year
  - **month** (**Required**, int): Month (1-12)

### Example

```yaml
- calendar:
    id: my_calendar
    x: 10
    y: 10
    today_date:
      year: 2024
      month: 1
      day: 16
    showed_date:
      year: 2024
      month: 1
    highlighted_dates:
      - year: 2024
        month: 1
        day: 25
    on_value:
      then:
        - lambda: |-
            // Handle date selection
```

---

## Chart (lv_chart)

Display data as line charts, bar charts, or scatter plots with multiple series support.

### Configuration Variables

- **type** (*Optional*, enum): Chart type. One of `LINE`, `BAR`, `SCATTER`, `NONE`. Defaults to `LINE`.
- **point_count** (*Optional*, int): Number of data points per series. Defaults to `10`.
- **update_mode** (*Optional*, enum): How data updates. One of `SHIFT`, `CIRCULAR`. Defaults to `SHIFT`.
- **series** (*Optional*, list): List of data series to display
  - **id** (**Required**, id): Series identifier
  - **color** (*Optional*, color): Series color
  - **type** (*Optional*, enum): Override chart type for this series
  - **points** (*Optional*, list): Initial data points (list of integers)
- **axis_primary_y** (*Optional*, dict): Primary Y axis configuration
  - **min_value** (*Optional*, int): Minimum value
  - **max_value** (*Optional*, int): Maximum value
  - **div_line_count** (*Optional*, int): Number of division lines
- **axis_secondary_y** (*Optional*, dict): Secondary Y axis configuration
- **axis_primary_x** (*Optional*, dict): Primary X axis configuration
- **axis_secondary_x** (*Optional*, dict): Secondary X axis configuration

All [common widget properties](#common-widget-properties) are supported.

### Widget Parts

- **main**: Chart background and border
- **items**: Data series and points

### Example

```yaml
- chart:
    id: temp_chart
    x: 10
    y: 10
    width: 300
    height: 200
    type: LINE
    point_count: 20
    update_mode: SHIFT
    axis_primary_y:
      min_value: 0
      max_value: 100
      div_line_count: 5
    series:
      - id: temperature_series
        color: 0xFF0000
      - id: humidity_series
        color: 0x0000FF
```

---

## Image Button (lv_imagebutton)

Button with different images for different states (pressed, released, disabled, checked).

### Configuration Variables

- **src_released** (*Optional*, image): Image for released state
- **src_pressed** (*Optional*, image): Image for pressed state
- **src_disabled** (*Optional*, image): Image for disabled state
- **src_checked_released** (*Optional*, image): Image for checked+released state
- **src_checked_pressed** (*Optional*, image): Image for checked+pressed state
- **src_checked_disabled** (*Optional*, image): Image for checked+disabled state

All [common widget properties](#common-widget-properties) are supported.

### Triggers

- **on_click**: Triggered when button is clicked
- **on_press**: Triggered when button is pressed
- **on_release**: Triggered when button is released

### Example

```yaml
- imgbtn:
    id: power_button
    x: 50
    y: 50
    src_released: power_off_icon
    src_pressed: power_off_pressed_icon
    src_checked_released: power_on_icon
    src_checked_pressed: power_on_pressed_icon
    on_click:
      then:
        - lambda: |-
            // Handle button click
```

---

## List (lv_list)

Scrollable list with button and text items. Perfect for menus and navigation.

### Configuration Variables

- **items** (*Optional*, list): List of items to display
  - **type** (**Required**, enum): Item type. One of `button`, `text`.
  - **text** (**Required**, string): Item text
  - **src** (*Optional*, image): Icon for button items (optional)

All [common widget properties](#common-widget-properties) are supported.

### Widget Parts

- **main**: List container background
- **scrollbar**: Scrollbar styling

### Example

```yaml
- list:
    id: main_menu
    x: 10
    y: 10
    width: 200
    height: 300
    bg_color: 0xFFFFFF
    items:
      - type: text
        text: "Menu Options"
      - type: button
        text: "Settings"
      - type: button
        text: "Wi-Fi Settings"
        src: wifi_icon
      - type: button
        text: "Display"
      - type: text
        text: "Actions"
      - type: button
        text: "Restart"
```

---

## Lottie (lv_lottie)

Display vector animations in Lottie JSON format using ThorVG rendering engine.

### Configuration Variables

- **src** (**Required**, string): Path to Lottie animation JSON file
- **autoplay** (*Optional*, boolean): Start animation automatically. Defaults to `true`.
- **loop** (*Optional*, boolean): Loop animation continuously. Defaults to `true`.

All [common widget properties](#common-widget-properties) are supported.

### Example

```yaml
- lottie:
    id: loading_animation
    x: CENTER
    y: CENTER
    width: 150
    height: 150
    src: "/animations/loading.json"
    autoplay: true
    loop: true
```

**Note**: Lottie animations require ThorVG support to be enabled in LVGL (`LV_USE_THORVG_INTERNAL = 1`).

---

## Menu (lv_menu)

Hierarchical navigation menu with pages, headers, and breadcrumb navigation.

### Configuration Variables

- **pages** (*Optional*, list): List of menu pages
  - **id** (**Required**, id): Page identifier
  - **title** (*Optional*, string): Page title text
  - **widgets** (*Optional*, list): Child widgets for page content
- **root_back_button** (*Optional*, boolean): Show back button on root page. Defaults to `false`.
- **mode** (*Optional*, enum): Menu header mode. One of `ROOT`, `HEADER`, `SIDEBAR`. Defaults to `HEADER`.
- **sidebar_page** (*Optional*, id): Reference to page used as sidebar

All [common widget properties](#common-widget-properties) are supported.

### Widget Parts

- **main**: Menu container
- **header**: Header/title bar area
- **sidebar**: Sidebar panel area

### Example

```yaml
- menu:
    id: settings_menu
    x: 0
    y: 0
    width: 100%
    height: 100%
    root_back_button: false
    pages:
      - id: main_page
        title: "Settings"
        widgets:
          - button:
              text: "Network"
          - button:
              text: "Display"

      - id: network_page
        title: "Network Settings"
        widgets:
          - switch:
              text: "WiFi Enabled"
```

---

## Scale (lv_scale)

Modern gauge/scale widget replacing the deprecated Meter widget. Displays values with ticks, labels, and colored sections.

### Configuration Variables

- **mode** (*Optional*, enum): Scale orientation. One of `ROUND_OUTER`, `ROUND_INNER`, `HORIZONTAL_TOP`, `HORIZONTAL_BOTTOM`, `VERTICAL_LEFT`, `VERTICAL_RIGHT`. Defaults to `ROUND_OUTER`.
- **min_value** (*Optional*, int): Minimum scale value. Defaults to `0`.
- **max_value** (*Optional*, int): Maximum scale value. Defaults to `100`.
- **rotation** (*Optional*, int): Start angle for round scales (degrees). Defaults to `0`.
- **angle_range** (*Optional*, int): Total angle coverage for round scales. Defaults to `270`.
- **ticks** (*Optional*, dict): Tick mark configuration
  - **count** (**Required**, int): Number of minor ticks
  - **width** (*Optional*, int): Tick line width
  - **length** (*Optional*, int): Tick length in pixels
  - **color** (*Optional*, color): Tick color
  - **major** (*Optional*, dict): Major tick configuration
    - **stride** (**Required**, int): Every Nth tick is major
    - **width** (*Optional*, int): Major tick width
    - **length** (*Optional*, int): Major tick length
    - **color** (*Optional*, color): Major tick color
    - **label_show** (*Optional*, boolean): Show labels on major ticks
    - **label_gap** (*Optional*, int): Gap between tick and label
- **sections** (*Optional*, list): Colored sections/zones
  - **id** (*Optional*, id): Section identifier
  - **range_from** (**Required**, int): Section start value
  - **range_to** (**Required**, int): Section end value
  - **color** (**Required**, color): Section color
  - **width** (*Optional*, int): Section arc width

All [common widget properties](#common-widget-properties) are supported.

### Widget Parts

- **main**: Scale background
- **items**: Tick marks and sections
- **indicator**: Value indicator/needle (if applicable)

### Example

```yaml
- scale:
    id: speed_gauge
    x: 10
    y: 10
    width: 200
    height: 200
    mode: ROUND_OUTER
    min_value: 0
    max_value: 200
    rotation: 135
    angle_range: 270
    ticks:
      count: 21
      width: 2
      length: 10
      color: 0x808080
      major:
        stride: 5
        width: 4
        length: 20
        color: 0xFFFFFF
        label_show: true
        label_gap: 8
    sections:
      - id: safe_zone
        range_from: 0
        range_to: 120
        color: 0x00FF00
        width: 6
      - id: danger_zone
        range_from: 160
        range_to: 200
        color: 0xFF0000
        width: 6
```

---

## Spangroup (lv_spangroup)

Display text with multiple styles (fonts, colors, decorations) within a single label.

### Configuration Variables

- **mode** (*Optional*, enum): Text overflow handling. One of `FIXED`, `EXPAND`, `BREAK`. Defaults to `BREAK`.
- **spans** (*Optional*, list): List of text spans with individual styling
  - **text** (**Required**, string): Span text content
  - **text_color** (*Optional*, color): Text color for this span
  - **text_font** (*Optional*, font): Font for this span
  - **text_decor** (*Optional*, enum): Text decoration. One of `NONE`, `UNDERLINE`, `STRIKETHROUGH`.

All [common widget properties](#common-widget-properties) are supported.

### Example

```yaml
- spangroup:
    id: rich_text
    x: 10
    y: 10
    width: 300
    mode: BREAK
    spans:
      - text: "Normal text "
        text_color: 0x000000
      - text: "Bold red text "
        text_color: 0xFF0000
        text_font: roboto_16_bold
      - text: "Underlined text"
        text_color: 0x0000FF
        text_decor: UNDERLINE
```

---

## Table (lv_table)

Display data in rows and columns with configurable cell content and column widths.

### Configuration Variables

- **row_count** (*Optional*, int): Number of table rows
- **column_count** (*Optional*, int): Number of table columns
- **cells** (*Optional*, list): Cell content configuration
  - **row** (**Required**, int): Row index (0-based)
  - **column** (**Required**, int): Column index (0-based)
  - **text** (**Required**, string): Cell text content
- **columns** (*Optional*, list): Column width configuration
  - **id** (**Required**, int): Column index (0-based)
  - **width** (**Required**, int): Column width in pixels

All [common widget properties](#common-widget-properties) are supported.

### Widget Parts

- **main**: Table background
- **items**: Individual table cells

### Example

```yaml
- table:
    id: sensor_table
    x: 10
    y: 10
    row_count: 4
    column_count: 2
    columns:
      - id: 0
        width: 120
      - id: 1
        width: 100
    cells:
      - row: 0
        column: 0
        text: "Sensor"
      - row: 0
        column: 1
        text: "Value"
      - row: 1
        column: 0
        text: "Temperature"
      - row: 1
        column: 1
        text: "25°C"
      - row: 2
        column: 0
        text: "Humidity"
      - row: 2
        column: 1
        text: "60%"
```

---

## 3D Texture (lv_3dtexture)

Experimental widget for displaying textures on 3D surfaces. Requires OpenGL backend.

### Configuration Variables

- **src** (**Required**, image): Texture image source
- **rotation_x** (*Optional*, float): Rotation around X axis. Defaults to `0`.
- **rotation_y** (*Optional*, float): Rotation around Y axis. Defaults to `0`.
- **rotation_z** (*Optional*, float): Rotation around Z axis. Defaults to `0`.
- **scale** (*Optional*, float): Texture scale factor. Defaults to `1.0`.

All [common widget properties](#common-widget-properties) are supported.

### Example

```yaml
- tex3d:
    id: texture_3d
    x: 50
    y: 50
    width: 200
    height: 200
    src: texture_image
    rotation_x: 45.0
    rotation_y: 30.0
    scale: 1.5
```

**Note**: 3D Texture widget is experimental and requires OpenGL rendering backend. Rarely used in embedded systems.

---

## Window (lv_win)

Modal window with title bar, header buttons, and content area.

### Configuration Variables

- **title** (**Required**, string): Window title text
- **header_height** (*Optional*, int): Title bar height in pixels. Defaults to `40`.
- **header_buttons** (*Optional*, list): Buttons to add to title bar
  - **id** (*Optional*, id): Button identifier
  - **src** (*Optional*, image): Button icon image

All [common widget properties](#common-widget-properties) are supported.

### Widget Parts

- **main**: Window container/background
- **header**: Title bar area
- **body**: Content area

### Example

```yaml
- win:
    id: settings_window
    title: "Settings"
    x: 50
    y: 50
    width: 400
    height: 300
    header_height: 50
    header:
      bg_color: 0x2196F3
      text_color: 0xFFFFFF
    header_buttons:
      - id: close_btn
        src: close_icon
        on_click:
          - lvgl.obj.add_flag:
              id: settings_window
              flag: HIDDEN
    widgets:
      - label:
          text: "Settings Content"
      - switch:
          text: "Enable Feature"
```

---

## Common Widget Properties

All widgets support these common properties:

### Positioning & Sizing
- **x** (*Optional*, int/percent): X coordinate
- **y** (*Optional*, int/percent): Y coordinate
- **width** (*Optional*, int/percent): Widget width
- **height** (*Optional*, int/percent): Widget height
- **align** (*Optional*, enum): Alignment relative to parent

### Styling
- **bg_color** (*Optional*, color): Background color
- **bg_opa** (*Optional*, opacity): Background opacity
- **border_width** (*Optional*, int): Border width in pixels
- **border_color** (*Optional*, color): Border color
- **pad_all** (*Optional*, int): Padding on all sides
- **pad_top** / **pad_bottom** / **pad_left** / **pad_right** (*Optional*, int): Individual padding
- **radius** (*Optional*, int): Corner radius
- **shadow_width** (*Optional*, int): Shadow blur width
- **shadow_color** (*Optional*, color): Shadow color
- **shadow_opa** (*Optional*, opacity): Shadow opacity

### Text Properties (for text-containing widgets)
- **text_color** (*Optional*, color): Text color
- **text_font** (*Optional*, font): Text font
- **text_align** (*Optional*, enum): Text alignment

### Flags
- **flag** (*Optional*, flag): Add widget flag (e.g., `HIDDEN`, `CLICKABLE`)

---

## Configuration Example

Complete example showing multiple widgets:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/youkorr/test2_esp_video_esphome
      ref: claude/fix-button-template-error-VHHoC
    components:
      - lvgl
      - font
      - image

lvgl:
  displays:
    - display_id: my_display
      pages:
        - id: main_page
          widgets:
            # Chart for sensor data
            - chart:
                id: temp_chart
                type: LINE
                point_count: 20
                series:
                  - id: temp_series
                    color: 0xFF0000

            # List menu
            - list:
                id: menu_list
                items:
                  - type: text
                    text: "Main Menu"
                  - type: button
                    text: "Settings"

            # Calendar
            - calendar:
                id: date_picker
                today_date:
                  year: 2024
                  month: 1
                  day: 16

            # Data table
            - table:
                id: data_table
                row_count: 3
                column_count: 2
                cells:
                  - row: 0
                    column: 0
                    text: "Sensor"
```

---

## References

- [LVGL v9.4 Official Documentation](https://docs.lvgl.io/9.4/)
- [ESPHome LVGL Component](https://esphome.io/components/lvgl/)
- [LVGL v9.4 Widgets Reference](https://docs.lvgl.io/9.4/details/widgets/index.html)

---

**Documentation Version:** 1.0
**LVGL Version:** 9.4.0
**Last Updated:** 2026-01-16
