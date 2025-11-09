# imlib - Utilisation avec ESPHome et mipi_dsi_cam

## Vue d'ensemble

La bibliothèque **imlib** fournit des fonctions de dessin optimisées pour images RGB565, permettant de dessiner directement sur les buffers caméra zero-copy sans copie de mémoire.

## Fonctionnalités

- ✅ Dessin de texte avec police Unicode 16x16
- ✅ Dessin de lignes, rectangles, cercles, ellipses
- ✅ Accès pixel individuel (get/set)
- ✅ Zero-copy : dessine directement sur buffer V4L2
- ✅ Optimisé pour RGB565 (format natif ESP32-P4)

## Intégration avec mipi_dsi_cam

Le composant `mipi_dsi_cam` expose automatiquement les fonctions imlib :

```cpp
// Dans votre lambda ESPHome
id(tab5_cam).draw_string(10, 10, "FPS: 30", 0xFFFF, 1.5);  // Texte blanc, scale 1.5x
id(tab5_cam).draw_line(0, 100, 1280, 100, 0xF800, 2);      // Ligne rouge, 2px
id(tab5_cam).draw_rectangle(50, 50, 200, 100, 0x07E0, 1, false);  // Rectangle vert
id(tab5_cam).draw_circle(640, 360, 50, 0x001F, 2, true);   // Cercle bleu rempli
```

## Couleurs RGB565

Format 16-bit : `RRRRR GGGGGG BBBBB`

Couleurs prédéfinies :
```cpp
0xFFFF  // Blanc
0x0000  // Noir
0xF800  // Rouge
0x07E0  // Vert
0x001F  // Bleu
0xFFE0  // Jaune
0xF81F  // Magenta
0x07FF  // Cyan
```

Conversion RGB888 → RGB565 :
```cpp
#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
```

## Exemple complet : Overlay FPS

```yaml
mipi_dsi_cam:
  id: tab5_cam
  sensor_type: sc202cs
  resolution: 720P
  pixel_format: RGB565

interval:
  - interval: 1s
    then:
      - lambda: |-
          static uint32_t frame_count = 0;
          static uint32_t last_fps = 0;

          // Calculer FPS
          auto fps = frame_count;
          frame_count = 0;

          // Dessiner FPS en haut à gauche
          char fps_text[32];
          snprintf(fps_text, sizeof(fps_text), "FPS: %lu", fps);
          id(tab5_cam).draw_string(10, 10, fps_text, 0xFFFF, 2.0);

          // Dessiner timestamp
          auto time = id(sntp_time).now();
          if (time.is_valid()) {
            char time_text[64];
            snprintf(time_text, sizeof(time_text), "%02d:%02d:%02d",
                     time.hour, time.minute, time.second);
            id(tab5_cam).draw_string(10, 50, time_text, 0x07E0, 1.5);
          }

          // Dessiner cadre de focus au centre
          int cx = 1280 / 2;
          int cy = 720 / 2;
          id(tab5_cam).draw_rectangle(cx - 100, cy - 100, 200, 200, 0xF800, 2, false);
          id(tab5_cam).draw_line(cx - 20, cy, cx + 20, cy, 0xF800, 1);
          id(tab5_cam).draw_line(cx, cy - 20, cx, cy + 20, 0xF800, 1);
```

## API Complète

### Dessin de texte
```cpp
void draw_string(int x, int y, const char *text, uint16_t color, float scale = 1.0f);
```
- `x, y` : Position (coin supérieur gauche)
- `text` : Chaîne C (support Unicode)
- `color` : Couleur RGB565
- `scale` : Échelle (1.0 = 16x16, 2.0 = 32x32, etc.)

### Dessin de ligne
```cpp
void draw_line(int x0, int y0, int x1, int y1, uint16_t color, int thickness = 1);
```

### Dessin de rectangle
```cpp
void draw_rectangle(int x, int y, int w, int h, uint16_t color, int thickness = 1, bool fill = false);
```

### Dessin de cercle
```cpp
void draw_circle(int cx, int cy, int radius, uint16_t color, int thickness = 1, bool fill = false);
```

### Accès pixel
```cpp
int get_pixel(int x, int y);                // Lire pixel RGB565
void set_pixel(int x, int y, uint16_t color);  // Écrire pixel RGB565
```

### Accès direct (avancé)
```cpp
image_t* get_imlib_image();  // Retourne pointeur vers structure imlib
```

## Performance

- **Zero-copy** : Dessine directement sur buffer V4L2, pas de copie mémoire
- **Optimisé** : Fonctions imlib écrites en C optimisé pour embedded
- **Impact FPS** : Minimal (<1 ms pour texte/formes simples)

## Limitations

- Format supporté : **RGB565 uniquement** (pas JPEG)
- Résolution max : Dépend de la RAM disponible (1280x720 = 1.8 MB)
- Police : Unicode 16x16 embarquée (2 MB)

## Utilisation avancée

Pour des opérations complexes, accéder directement à l'API imlib :

```cpp
image_t *img = id(tab5_cam).get_imlib_image();
if (img) {
  // Utiliser directement les fonctions imlib.h
  imlib_draw_ellipse(img, cx, cy, rx, ry, rotation, color, thickness, fill);
}
```

Voir `/components/imlib/include/imlib.h` pour l'API complète.
