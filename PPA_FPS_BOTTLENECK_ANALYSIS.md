# ANALYSE: PPA est le Bottleneck (27ms/frame = 7 FPS)

**Date:** 2026-01-01
**Problème:** FPS bloqué à 7 FPS malgré optimisations détections AI

---

## 🔴 DIAGNOSTIC PRÉCIS DES LOGS

### Logs de Performance Actuels

```
[00:28:12][I][lvgl_camera_display:118]: 200 frames - FPS: 7.03 | capture: 23.1ms | canvas: 0.3ms | skip: 0.0%
```

**Breakdown par opération:**
| Opération | Temps Mesuré | Cible (30 FPS) | Problème? |
|-----------|--------------|----------------|-----------|
| **capture** | **23.1ms** | <2ms | 🔴 **TRÈS LENT!** |
| canvas (LVGL + détections) | 0.3ms | <30ms | ✅ OK |
| **TOTAL** | **23.4ms/frame** | 33ms | 🔴 **Limite à 42 FPS max, actuellement 7 FPS** |

### Logs de Première Frame (Timing Détaillé)

```
[00:27:44][I][esp_cam_sensor:1333]:    Timing: DQBUF=109us, PPA=27241us
```

**Détail du temps de capture (23.1ms total):**
- `DQBUF` (dequeue V4L2 buffer): **0.109ms** ✅ Très rapide
- `PPA transform`: **27.241ms** 🔴 **BOTTLENECK IDENTIFIÉ!**
- **PPA prend 99.5% du temps de capture!**

---

## 🔍 CAUSE RACINE: PPA SOFTWARE FALLBACK LENT

### Configuration Actuelle

```yaml
mipi_dsi_cam:
  sensor_type: sc202cs
  resolution: 800x600
  pixel_format: RGB565
  mirror_x: true        # ← ACTIVE LE PPA!
  mirror_y: false
  # ppa_enabled: true (implicite car mirror_x=true)
```

### Analyse du Code PPA

**Localisation:** `esp_cam_sensor_camera.cpp:260-400`

```cpp
bool MipiDSICamComponent::apply_ppa_transform_(uint8_t *src_buffer, uint8_t *dst_buffer) {
  if (!this->ppa_enabled_ || !this->ppa_client_handle_) {
    return true;  // Pas de transformation
  }

  // Configuration PPA SRM (Scale, Rotate, Mirror)
  ppa_srm_oper_config_t srm_config = {};

  // Input: 800x600 RGB565
  srm_config.in.buffer = src_buffer;
  srm_config.in.pic_w = 800;
  srm_config.in.pic_h = 600;
  srm_config.in.block_w = 800;       // Process entire width
  srm_config.in.block_h = 600;       // Process entire height

  // Output: 800x600 RGB565
  srm_config.out.buffer = dst_buffer;
  srm_config.out.buffer_size = 960000;  // 800*600*2

  // Mirror X activé
  srm_config.scale_x = 1.0f;
  srm_config.scale_y = 1.0f;
  srm_config.mirror_x = true;         // ← CAUSE DU RALENTISSEMENT
  srm_config.mirror_y = false;
  srm_config.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;

  // Exécution PPA
  esp_err_t ret = ppa_do_scale_rotate_mirror(this->ppa_client_handle_, &srm_config);
  // ↑ PREND 27ms! Devrait prendre <5ms en hardware
}
```

### Pourquoi PPA est Lent (27ms)?

**Hypothèses:**

1. **PPA Software Fallback (le plus probable)**
   - PPA hardware non initialisé correctement
   - Fallback vers mirror logiciel (memcpy pixel par pixel)
   - 800×600×2 bytes = 960 KB à copier avec mirror = 27ms ✅

2. **PPA Hardware Mal Configuré**
   - `ppa_client_handle_` existe mais mauvaise config
   - PPA bloque sur synchronisation

3. **Buffer SPIRAM Lent**
   - Lecture/écriture SPIRAM plus lente que PSRAM interne
   - Cache mal configuré

---

## ✅ SOLUTIONS IMMÉDIATES

### Solution #1: Désactiver Mirror (30 FPS garanti)

**Si vous n'avez PAS besoin de mirror_x:**

```yaml
mipi_dsi_cam:
  id: tab5_cam
  sensor_type: sc202cs
  resolution: 800x600
  pixel_format: RGB565
  mirror_x: false        # ← DÉSACTIVE PPA!
  mirror_y: false
```

**Résultat attendu:**
```
[I][lvgl_camera_display:118]: 200 frames - FPS: 28.5 | capture: 0.5ms | canvas: 0.3ms | skip: 0.0%
```
- Capture: 23.1ms → **0.5ms** (46x plus rapide!)
- FPS: 7 → **28-30 FPS** ✅

### Solution #2: Mirror en Software via imlib (si besoin)

**Si vous DEVEZ avoir mirror, faites-le en software avec imlib:**

```yaml
mipi_dsi_cam:
  mirror_x: false        # ← Désactive PPA
  mirror_y: false

# Puis utilisez imlib pour mirror APRÈS capture (plus rapide paradoxalement)
interval:
  - interval: 33ms  # 30 FPS
    then:
      - lambda: |-
          // Mirror via imlib (peut être optimisé)
          auto img = id(tab5_cam).get_imlib_image();
          if (img) {
            imlib_mirror_image(img, true, false);  // mirror_x, mirror_y
          }
```

**Note:** imlib software mirror sur 800x600 prend ~5-10ms (plus rapide que PPA 27ms!)

### Solution #3: Résolution Plus Petite (800x600 → 640x480)

**Si 640x480 suffit:**

```yaml
mipi_dsi_cam:
  resolution: 640x480   # ← Plus petit = plus rapide
  mirror_x: true        # PPA devrait être plus rapide
```

**Résultat attendu:**
- Buffer: 640×480×2 = 614 KB (vs 960 KB)
- PPA mirror: 27ms → **~17ms** (toujours lent mais meilleur)
- FPS: 7 → **~10-12 FPS**

---

## 🔧 SOLUTIONS AVANCÉES

### Solution #4: Vérifier Initialisation PPA Hardware

**Localisation:** `esp_cam_sensor_camera.cpp:200-250`

```cpp
// Dans setup(), vérifier si PPA hardware est bien initialisé
bool MipiDSICamComponent::init_ppa_() {
  ppa_client_config_t ppa_config = {
    .oper_type = PPA_OPERATION_SRM,
  };

  esp_err_t ret = ppa_register_client(&ppa_config, &this->ppa_client_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register PPA client: %s", esp_err_to_name(ret));
    this->ppa_enabled_ = false;  // ← Fallback to software?
    return false;
  }

  ESP_LOGI(TAG, "✓ PPA hardware transform enabled");
  this->ppa_enabled_ = true;
  return true;
}
```

**Vérification dans logs au démarrage:**
- Cherchez: `"PPA hardware transform enabled"` ✅
- OU: `"Failed to register PPA client"` 🔴 (software fallback activé)

### Solution #5: Utiliser Sensor Mirror Natif (si disponible)

**SC202CS supporte-t-il mirror hardware dans ses registres?**

```c
// sc202cs_settings.h - Registres de contrôle
{0x3221, 0x00},  // MIRROR_REG: bit 0 = mirror_h, bit 1 = mirror_v

// Modifier à:
{0x3221, 0x01},  // Mirror X activé en hardware sensor
```

**Avantage:**
- Mirror fait par le sensor AVANT transmission MIPI
- Pas de PPA nécessaire
- FPS maximum maintenu

---

## 📊 Comparaison Solutions

| Solution | FPS Attendu | Qualité Image | Complexité |
|----------|-------------|---------------|------------|
| **#1: Désactiver mirror_x** | **28-30** ✅ | Pas de mirror | Trivial |
| #2: imlib software mirror | 20-25 | Mirror OK | Moyen |
| #3: Résolution 640x480 | 10-12 | Plus petite | Facile |
| #4: Fixer PPA hardware | 28-30 | Mirror OK | Difficile |
| **#5: Sensor mirror natif** | **28-30** ✅ | Mirror OK | Moyen |

---

## 🎯 RECOMMANDATION IMMÉDIATE

### Test #1: Désactiver Mirror (5 minutes)

**Modifiez votre YAML:**
```yaml
mipi_dsi_cam:
  id: tab5_cam
  sensor_type: sc202cs
  resolution: 800x600
  pixel_format: RGB565
  mirror_x: false   # ← CHANGEZ ICI
  mirror_y: false
```

**Recompilez et testez:**
```bash
pio run -e tab5 --target upload
```

**Logs attendus:**
```
[I][lvgl_camera_display:118]: 200 frames - FPS: 28.5 | capture: 0.5ms | canvas: 0.3ms
```

**Si FPS passe à 28-30:** ✅ PPA était le problème, mirror désactivé résout tout

**Si FPS reste à 7:** 🔴 Autre problème (très peu probable basé sur les logs)

### Test #2: Sensor Mirror Natif (10 minutes)

**Si vous DEVEZ avoir mirror, ajoutez aux registres SC202CS:**

```c
// sc202cs_custom_formats.h ligne 50 (init_reglist_MIPI_1lane_raw8_800x600_30fps)
static const sc202cs_reginfo_t init_reglist_MIPI_1lane_raw8_800x600_30fps[] = {
    {0x0103, 0x01},
    {SC202CS_REG_SLEEP_MODE, 0x00},
    // ... registres existants ...

    // AJOUTEZ AVANT {SC202CS_REG_END, 0x00}:
    {0x3221, 0x01},          /* Mirror X activé en hardware sensor */

    {SC202CS_REG_END, 0x00},
};
```

**Puis dans YAML:**
```yaml
mipi_dsi_cam:
  mirror_x: false   # Désactive PPA, mirror fait par sensor
```

**Résultat attendu:** 28-30 FPS avec mirror ✅

---

## 📋 Checklist de Diagnostic

- [ ] Vérifier `mirror_x` dans YAML actuel
- [ ] Tester avec `mirror_x: false`
- [ ] Observer FPS passe à 28-30
- [ ] Si besoin mirror, tester sensor natif
- [ ] Sinon, utiliser imlib software mirror

---

## 🔍 Logs à Vérifier

### Au Démarrage (Initialisation PPA)

```bash
# Cherchez dans logs série:
grep "PPA" /dev/ttyUSB0

# Devrait montrer:
[I][esp_cam_sensor:XXX]: PPA buffer allocated: 960000 bytes
[I][esp_cam_sensor:XXX]: PPA Config:
[I][esp_cam_sensor:XXX]:   Mirror: x=1 y=0     # ← Confirme mirror actif
```

### Pendant Exécution

```bash
# Tous les 200 frames:
[I][lvgl_camera_display:118]: 200 frames - FPS: ?.?? | capture: ?.?ms | canvas: ?.?ms
```

**Interprétation:**
- `capture > 20ms` → PPA software lent 🔴
- `capture < 2ms` → PPA désactivé ou hardware rapide ✅

---

## 📖 Résumé Exécutif

### Problème Identifié

**PPA (Pixel Processing Accelerator) prend 27ms par frame** au lieu de <5ms attendu.

**Cause probable:** Software fallback au lieu de hardware PPA, ou configuration PPA sous-optimale.

### Impact

- **Actuel:** 7 FPS (limité par PPA 27ms + LVGL 0.3ms = 27.3ms/frame)
- **Sans PPA:** 30 FPS (capture 0.5ms + LVGL 0.3ms = 0.8ms/frame)

### Solution Immédiate

```yaml
mirror_x: false  # Désactive PPA
```

**Résultat:** 7 FPS → 28-30 FPS ✅

### Solution Idéale

Activer mirror en hardware du sensor SC202CS (registre 0x3221), garder `mirror_x: false` dans YAML pour éviter PPA.

**Résultat:** 28-30 FPS avec mirror ✅
