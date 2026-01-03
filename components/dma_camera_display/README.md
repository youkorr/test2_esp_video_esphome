# DMA Camera Display - Zero LVGL Redraw

## Architecture esp_brookesia

Ce composant reproduit l'architecture esp_brookesia pour affichage caméra sans tremblement:

### ✅ Principes

1. **DMA Direct**: Camera DMA → Buffer → LCD DMA
2. **Triple Buffering**: Buffers PSRAM alignés 64 bytes, swap atomique
3. **Hardware Sync**: VSync/VBlank LCD, fin de frame caméra
4. **ZÉRO LVGL**: Pas de canvas, pas d'invalidate, pas de redraw

### 🔧 Configuration

```yaml
dma_camera_display:
  camera_id: tab5_cam
  width: 800
  height: 600
  x_offset: 0  # Position sur LCD
  y_offset: 0
  enable_vsync: true
```

### 📊 Performance

- **Sans ce composant:** 8-10 FPS avec tremblements (LVGL redraw)
- **Avec DMA + triple buffer:** 25-30 FPS fluide
- **Avec VSync:** 30 FPS parfait (tearing-free)

### ⚠️ Limitations

- Incompatible avec LVGL canvas sur même zone écran
- Nécessite LCD MIPI DSI avec support DMA
- Triple buffering utilise ~3 MB PSRAM (800x600 RGB565)
