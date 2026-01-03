# Architecture DMA Direct - Camera Sans LVGL Redraw

## 🔥 Problème Identifié

L'implémentation actuelle (`lvgl_camera_display` + `simple_camera_test`) utilise:
- ❌ LVGL canvas (redraw forcé)
- ❌ `lv_refr_now()` ou `lv_obj_invalidate()` (copie mémoire CPU)
- ❌ Pas de synchronisation VSync
- ❌ Pas de vrai triple buffering DMA

**Résultat:** Maximum 9-10 FPS avec tremblements, même si sensor génère 27-30 FPS.

## ✅ Architecture esp_brookesia (Solution Correcte)

### 1. Pipeline DMA Direct
```
Capteur SC202CS
    ↓ MIPI CSI (DMA)
Buffer A (PSRAM, aligned 64 bytes)
    ↓ Swap atomique (VSync)
Buffer B (PSRAM, aligned 64 bytes)
    ↓ LCD DMA (esp_lcd_panel_draw_bitmap)
Écran MIPI DSI
```

### 2. Triple Buffering Réel
```c
uint8_t *camera_buffer;   // Buffer caméra (lecture DMA CSI)
uint8_t *display_buffer;  // Buffer LCD (envoi DMA LCD)
uint8_t *swap_buffer;     // Buffer intermédiaire (swap atomique)

// Tous alignés 64 bytes en PSRAM
#define BUFFER_SIZE (width * height * 2)  // RGB565
camera_buffer = heap_caps_aligned_alloc(64, BUFFER_SIZE, MALLOC_CAP_SPIRAM);
display_buffer = heap_caps_aligned_alloc(64, BUFFER_SIZE, MALLOC_CAP_SPIRAM);
swap_buffer = heap_caps_aligned_alloc(64, BUFFER_SIZE, MALLOC_CAP_SPIRAM);
```

### 3. Synchronisation Hardware
```c
// Attendre fin de frame caméra (ISR CSI)
void IRAM_ATTR csi_frame_done_isr(void *arg) {
    // Frame caméra prête dans camera_buffer
    xSemaphoreGiveFromISR(frame_ready_sem, NULL);
}

// Swap atomique synchronisé avec LCD VSync
void camera_task(void *arg) {
    while (1) {
        // Attendre frame caméra
        xSemaphoreTake(frame_ready_sem, portMAX_DELAY);

        // Swap instantané (pointeurs seulement)
        uint8_t *temp = swap_buffer;
        swap_buffer = camera_buffer;
        camera_buffer = temp;

        // Attendre VSync LCD (optionnel mais recommandé)
        // esp_lcd_panel_wait_for_vsync(panel_handle);

        // Envoyer au LCD via DMA (non-bloquant)
        esp_lcd_panel_draw_bitmap(panel_handle,
                                   x, y,
                                   x + width, y + height,
                                   swap_buffer);
    }
}
```

### 4. ZÉRO Interaction LVGL
```c
// ❌ PAS DE:
lv_canvas_set_buffer(...)      // Copie CPU
lv_obj_invalidate(...)          // Redraw hiérarchie
lv_refr_now(...)                // Refresh LVGL
lv_img_set_src(...)             // Décodage image

// ✅ FAIRE:
esp_lcd_panel_draw_bitmap(...)  // DMA direct LCD
```

## 📋 Implémentation Proposée

### Option 1: Utiliser esp_brookesia Directement
Si disponible dans ESP-IDF, intégrer le composant esp_brookesia qui gère déjà:
- Camera DMA
- Triple buffering
- VSync sync
- LCD DMA direct

### Option 2: Créer `dma_camera_display` Component
Nouveau composant ESPHome qui:

```cpp
class DmaCameraDisplay : public Component {
 protected:
  // Handles ESP-IDF directs
  esp_lcd_panel_handle_t lcd_panel_{nullptr};
  esp_cam_ctlr_handle_t cam_handle_{nullptr};

  // Triple buffering PSRAM
  uint8_t *camera_buf_{nullptr};
  uint8_t *display_buf_{nullptr};
  uint8_t *swap_buf_{nullptr};

  // Synchronisation
  SemaphoreHandle_t frame_sem_{nullptr};
  TaskHandle_t display_task_{nullptr};

  void setup() override {
    // 1. Récupérer lcd_panel depuis LVGL display
    // 2. Initialiser camera CSI DMA
    // 3. Allouer buffers PSRAM alignés
    // 4. Créer task DMA
  }

  static void display_task_func(void *arg) {
    while (1) {
      // Attendre frame camera
      // Swap atomique
      // esp_lcd_panel_draw_bitmap()
    }
  }
};
```

### Option 3: Patch lvgl_camera_display
Modifier le composant existant pour:
1. Désactiver LVGL canvas quand caméra active
2. Utiliser `esp_lcd_panel_draw_bitmap()` direct
3. Ajouter triple buffering + VSync

## 🎯 Priorités

1. **Identifier le handle LCD panel** utilisé par ESPHome LVGL
2. **Tester DMA direct** avec un seul buffer (proof of concept)
3. **Ajouter triple buffering** si FPS OK
4. **Synchroniser VSync** si tremblements persistent

## 📊 Performance Attendue

- **Sans DMA:** 8-10 FPS (actuel)
- **Avec DMA + triple buffer:** 25-30 FPS fluide
- **Avec VSync:** 30 FPS parfait (tearing-free)

## 🔧 Code de Test Minimal

```cpp
// Récupérer LCD panel depuis LVGL (à faire dans setup)
extern "C" {
  lv_disp_t *disp = lv_disp_get_default();
  // TODO: extraire esp_lcd_panel_handle_t de disp->driver->user_data
}

// Test DMA direct (remplace LVGL canvas)
void update_camera_dma(uint8_t *frame_data) {
  esp_lcd_panel_draw_bitmap(
    lcd_panel_handle,
    x_offset, y_offset,
    x_offset + frame_width, y_offset + frame_height,
    frame_data  // Buffer caméra direct (RGB565)
  );
}
```

## ❓ Questions pour Utilisateur

1. Avez-vous accès au composant `esp_brookesia` dans votre ESP-IDF?
2. Préférez-vous que je:
   - A) Intègre esp_brookesia directement
   - B) Crée un composant DMA custom
   - C) Patche lvgl_camera_display pour utiliser DMA

3. Le LCD est-il configuré via ESPHome display ou via code custom?
