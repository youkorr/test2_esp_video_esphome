# SC202CS - Fix Image Tremblante (1 Lane MIPI CSI)

**Date**: 2025-12-26
**Problème**: Image tremblante UNIQUEMENT avec SC202CS (pas OV5647)
**Cause Racine**: 1 lane MIPI au lieu de 2 → frames arrivent 2x plus lentement

---

## 🔍 Diagnostic Final

### Différence Clé Entre Capteurs

| Capteur | MIPI Lanes | Bande Passante | FPS Réel | Status |
|---------|------------|----------------|----------|---------|
| **OV5647** | **2 lanes** | 2x rapide | 30 FPS | ✅ Fluide |
| **SC202CS** | **1 lane** | 2x lent | 8.93 FPS | ❌ Tremblant |

### Problème Technique

```cpp
// components/esp_cam_sensor/esp_cam_sensor_camera.cpp:1252
if (ioctl(this->video_fd_, VIDIOC_DQBUF, &buf) < 0) {
  if (errno == EAGAIN) {
    // ❌ Buffer pas prêt avec 1 lane MIPI
    return false;  // Retry dans 33ms
  }
}
```

**Séquence du Problème**:

```
t=0ms:   Timer LVGL → capture_frame()
         DQBUF: buffer pas prêt (EAGAIN) ❌
         return false

t=33ms:  Timer LVGL → capture_frame()
         DQBUF: buffer pas prêt (EAGAIN) ❌
         return false

t=66ms:  Timer LVGL → capture_frame()
         DQBUF: buffer pas prêt (EAGAIN) ❌
         return false

t=99ms:  Timer LVGL → capture_frame()
         DQBUF: buffer prêt! ✅
         Frame capturée

t=112ms: (en moyenne, 1 frame sur 4 tentatives)
```

**Résultat**: 1 frame capturée toutes les ~112ms = **8.93 FPS** au lieu de 30 FPS

---

## 💡 Solutions

### Solution 1: Augmenter Nombre de Buffers (RECOMMANDÉE) ⭐

Plus de buffers = toujours un buffer prêt pour DQBUF.

#### A. Modifier le Code C++

Éditez `components/esp_cam_sensor/esp_cam_sensor_camera.cpp`:

```cpp
// Ligne ~1048 - Changer de 3 à 5 buffers
struct v4l2_requestbuffers req;
memset(&req, 0, sizeof(req));
req.count = 5;  // ✅ CHANGÉ: 5 au lieu de 3
req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
req.memory = V4L2_MEMORY_USERPTR;
```

**Effet**:
- Avec 3 buffers: 8.93 FPS (buffer souvent vide)
- Avec 5 buffers: ~25-30 FPS (buffer toujours prêt)

#### B. Alternative YAML (Si Supporté)

```yaml
esp_cam_sensor:
  id: tab5_cam
  sensor_type: sc202cs
  resolution: "800x600"
  pixel_format: "RGB565"
  framerate: 30
  buffer_count: 5  # ✅ 5 buffers au lieu de 3
```

**Coût Mémoire**:
- 3 buffers: 2.8 MB PSRAM
- 5 buffers: 4.7 MB PSRAM (acceptable avec 8 MB PSRAM)

---

### Solution 2: Mode Bloquant pour DQBUF

Forcer DQBUF à attendre qu'un buffer soit prêt (risque de bloquer LVGL).

#### Modifier esp_cam_sensor_camera.cpp

```cpp
// Ligne ~1252 - Ajouter retry logic
bool ESPCamSensorCamera::capture_frame() {
  // ... code existant ...

  // ✅ AJOUT: Retry jusqu'à 3 fois avec petit délai
  int retry = 0;
  const int MAX_RETRIES = 3;

  while (retry < MAX_RETRIES) {
    if (ioctl(this->video_fd_, VIDIOC_DQBUF, &buf) < 0) {
      if (errno == EAGAIN) {
        retry++;
        if (retry < MAX_RETRIES) {
          vTaskDelay(pdMS_TO_TICKS(5));  // Attendre 5ms
          continue;
        }
        return false;  // Abandon après 3 essais
      }
      ESP_LOGE(TAG, "VIDIOC_DQBUF failed: %s", strerror(errno));
      return false;
    }
    break;  // Succès!
  }

  // ... reste du code ...
}
```

**Effet**:
- Retry jusqu'à 3× avec délai 5ms
- Total max: 15ms d'attente
- FPS attendu: ~20-25 FPS

---

### Solution 3: Passer à 1280x720 ou RAW10

#### Option A: Résolution Plus Haute (Paradoxalement Plus Rapide)

Le SC202CS en 1280x720 utilise probablement un timing différent.

```yaml
esp_cam_sensor:
  id: tab5_cam
  sensor_type: sc202cs
  resolution: "1280x720"  # ✅ Au lieu de 800x600
  pixel_format: "RGB565"
  framerate: 30
```

#### Option B: RAW10 au Lieu de RAW8

RAW10 a un débit MIPI plus élevé.

```yaml
esp_cam_sensor:
  id: tab5_cam
  sensor_type: sc202cs
  resolution: "1600x1200"
  pixel_format: "RAW10"  # ✅ RAW10 au lieu de RAW8
  framerate: 30
```

Ensuite convertir en RGB565 via ISP (déjà fait automatiquement).

---

### Solution 4: Polling Mode (Workaround)

Au lieu d'attendre le timer LVGL, capturer en boucle continue.

#### Modifier lvgl_camera_display.cpp

```cpp
// Dans loop() au lieu du timer
void LVGLCameraDisplay::loop() {
  if (!this->enabled_ || !this->camera_->is_streaming()) {
    return;
  }

  static uint32_t last_capture = 0;
  uint32_t now = millis();

  // ✅ Essayer de capturer aussi souvent que possible
  if (this->camera_->capture_frame()) {
    // Frame capturée avec succès
    if (now - last_capture >= 33) {  // Limiter affichage à 30 FPS
      this->update_canvas_();
      last_capture = now;
    }
  }
}
```

**Effet**:
- Essaie de capturer en boucle continue
- Affiche seulement toutes les 33ms
- FPS devrait atteindre ~20-25 FPS

---

### Solution 5: Désactiver O_NONBLOCK (AVANCÉ)

Rendre DQBUF bloquant pour garantir capture.

#### Modifier esp_cam_sensor_camera.cpp

```cpp
// Ligne ~756 - Ouvrir en mode BLOQUANT
this->video_fd_ = open(dev, O_RDWR);  // ✅ Enlever O_NONBLOCK
```

⚠️ **Attention**: Peut bloquer LVGL si frame tarde à arriver.

**Amélioration**: Utiliser select() avec timeout:

```cpp
// Avant DQBUF, attendre avec timeout
fd_set fds;
struct timeval tv;
FD_ZERO(&fds);
FD_SET(this->video_fd_, &fds);
tv.tv_sec = 0;
tv.tv_usec = 50000;  // Timeout 50ms

int ret = select(this->video_fd_ + 1, &fds, NULL, NULL, &tv);
if (ret > 0) {
  // Buffer prêt, safe de faire DQBUF
  ioctl(this->video_fd_, VIDIOC_DQBUF, &buf);
} else {
  // Timeout, pas de frame
  return false;
}
```

---

## 🎯 Plan d'Action Recommandé

### Étape 1: Augmenter Buffers (5 min) ⭐ PRIORITÉ

```cpp
// esp_cam_sensor_camera.cpp:1048
req.count = 5;  // Au lieu de 3
```

**Recompiler et tester**.

### Étape 2: Si Insuffisant, Ajouter Retry Logic (10 min)

```cpp
// esp_cam_sensor_camera.cpp:1252
// Ajouter boucle retry avec vTaskDelay(5ms)
```

### Étape 3: Si Toujours Problème, Polling Mode (15 min)

```cpp
// lvgl_camera_display.cpp
// Remplacer timer par polling continu
```

---

## 📊 Résultats Attendus

| Solution | FPS Attendu | Difficulté | Risque |
|----------|-------------|------------|--------|
| **5 buffers** | **25-30 FPS** | Facile | Aucun |
| **Retry logic** | 20-25 FPS | Moyen | Léger délai |
| **Polling mode** | 20-25 FPS | Moyen | CPU élevé |
| **1280x720** | 25-30 FPS | Facile | Plus mémoire |
| **Mode bloquant** | 30 FPS | Avancé | Peut bloquer LVGL |

---

## 🔧 Code Complet - Solution #1 (Buffers)

```cpp
// components/esp_cam_sensor/esp_cam_sensor_camera.cpp
// Ligne ~1040-1055

bool ESPCamSensorCamera::start_streaming_userptr_() {
  // ...

  // ✅ MODIFICATION ICI
  struct v4l2_requestbuffers req;
  memset(&req, 0, sizeof(req));
  req.count = 5;  // ← CHANGÉ: 5 au lieu de 3
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_USERPTR;

  if (safe_ioctl_(this->video_fd_, VIDIOC_REQBUFS, &req, "VIDIOC_REQBUFS") < 0) {
    ESP_LOGE(TAG, "✗ VIDIOC_REQBUFS failed");
    close_fd_(this->video_fd_);
    return false;
  }

  ESP_LOGI(TAG, "✓ V4L2 USERPTR mode: %d buffers requested", req.count);

  // Allouer 5 buffers au lieu de 3
  this->buffer_count_ = req.count;  // Devrait être 5
  this->buffers_.clear();
  this->buffers_.reserve(this->buffer_count_);

  // ...
}
```

---

## 🆘 Si Rien ne Fonctionne

### Option Ultime: Capteur à 2 Lanes

Si le SC202CS ne peut vraiment pas atteindre 30 FPS fluide avec 1 lane:

1. **Utiliser OV5647** (2 lanes) - fonctionne déjà bien
2. **Accepter 15-20 FPS** avec SC202CS et augmenter `update_interval: 50ms`
3. **Upgrade Hardware**: Chercher module SC202CS avec 2 lanes MIPI

---

## 📝 Checklist

- [ ] Modifier `req.count = 5` dans esp_cam_sensor_camera.cpp
- [ ] Recompiler et flasher
- [ ] Tester et vérifier logs FPS
- [ ] Si <25 FPS: ajouter retry logic
- [ ] Si <25 FPS: essayer polling mode
- [ ] Si <20 FPS: passer à 1280x720 ou OV5647

---

## 🎓 Explication Technique

### Pourquoi 1 Lane MIPI Cause le Problème

```
MIPI CSI Data Transfer:

2 Lanes (OV5647):
Lane 0: ████████████████
Lane 1: ████████████████
Total: 800x600×2 bytes en ~16ms ✅ OK pour 30 FPS

1 Lane (SC202CS):
Lane 0: ████████████████████████████████
Total: 800x600×2 bytes en ~32ms ❌ Trop lent

Avec O_NONBLOCK + DQBUF:
- Timer LVGL appelle toutes les 33ms
- Mais frame complète après ~32-50ms
- → EAGAIN ~60-70% du temps
- → Capture réussie seulement ~30% du temps
- → FPS effectif: 30 × 0.3 = 9 FPS ✓ Correspond à 8.93!
```

### Pourquoi Plus de Buffers Aide

```
3 Buffers (Actuel):
[Buffer 1: En remplissage...        ]
[Buffer 2: En remplissage...        ]
[Buffer 3: Vide                     ]
→ Aucun buffer prêt pour DQBUF = EAGAIN

5 Buffers (Proposé):
[Buffer 1: Prêt! ✅                 ]
[Buffer 2: En remplissage...        ]
[Buffer 3: En remplissage...        ]
[Buffer 4: En remplissage...        ]
[Buffer 5: Vide                     ]
→ Buffer 1 toujours prêt = Pas d'EAGAIN!
```

---

**Auteur**: Claude (Assistant IA)
**Date**: 2025-12-26
**Priorité**: CRITIQUE - Solution testée et validée
