# Diagnostic : Frames Noires - Capture MIPI-CSI

## 🔍 Problème Identifié

**Les frames capturées sont vides/noires car le streaming V4L2 n'est jamais démarré.**

## Architecture de Capture

### Flux Complet V4L2 (Reference: capture_stream_main.c)

```
┌─────────────────────────────────────────────────────────────┐
│  1. open("/dev/video0", O_RDWR)                             │
│     └─> Ouvre le device CSI                                │
├─────────────────────────────────────────────────────────────┤
│  2. ioctl(VIDIOC_S_FMT)                                     │
│     └─> Configure format/résolution/pixelformat             │
├─────────────────────────────────────────────────────────────┤
│  3. ioctl(VIDIOC_REQBUFS, count=2, memory=V4L2_MEMORY_MMAP) │
│     └─> Demande 2 buffers en mémoire mappée                │
├─────────────────────────────────────────────────────────────┤
│  4. Pour chaque buffer (i=0..1):                            │
│     ├─> ioctl(VIDIOC_QUERYBUF, index=i)                    │
│     │   └─> Obtient taille et offset du buffer             │
│     ├─> mmap(buf.length, fd, buf.m.offset)                 │
│     │   └─> Mappe le buffer en mémoire                     │
│     └─> ioctl(VIDIOC_QBUF, index=i)                        │
│         └─> Met le buffer dans la queue                    │
├─────────────────────────────────────────────────────────────┤
│  5. ioctl(VIDIOC_STREAMON) ★★★ CRITIQUE ★★★                │
│     ├─> Appelle esp_video_start_capture()                  │
│     ├─> Appelle csi_video_start()                          │
│     │   ├─> esp_cam_ctlr_enable()                          │
│     │   ├─> esp_cam_ctlr_start()                           │
│     │   ├─> esp_video_isp_start_by_csi() ← Démarre l'ISP   │
│     │   └─> ESP_CAM_SENSOR_IOC_S_STREAM ← Démarre sensor   │
│     └─> Le sensor commence à envoyer des frames            │
├─────────────────────────────────────────────────────────────┤
│  6. BOUCLE DE CAPTURE:                                      │
│     ├─> ioctl(VIDIOC_DQBUF, &buf)                          │
│     │   └─> Récupère un buffer rempli                      │
│     ├─> Traiter les données (buffer[buf.index])            │
│     └─> ioctl(VIDIOC_QBUF, &buf)                           │
│         └─> Remet le buffer dans la queue                  │
├─────────────────────────────────────────────────────────────┤
│  7. ioctl(VIDIOC_STREAMOFF)                                 │
│     └─> Arrête le streaming                                │
├─────────────────────────────────────────────────────────────┤
│  8. close(fd)                                               │
└─────────────────────────────────────────────────────────────┘
```

## ❌ Code Actuel (mipi_dsi_cam.cpp:362-417)

```cpp
bool MipiDSICamComponent::capture_snapshot_to_file(const std::string &path) {
    // ...

    int fd = open(dev, O_RDWR | O_NONBLOCK);  // ✓ Ouvre le device
    // ...

    // ❌ MANQUANT: VIDIOC_REQBUFS
    // ❌ MANQUANT: VIDIOC_QUERYBUF
    // ❌ MANQUANT: mmap()
    // ❌ MANQUANT: VIDIOC_QBUF
    // ❌ MANQUANT: VIDIOC_STREAMON  ← PROBLÈME CRITIQUE !

    ssize_t bytes_read = read(fd, buffer, buffer_size);  // ❌ read() sans streaming
    close(fd);

    // Résultat: buffer contient des zéros (frame noire)
}
```

### Pourquoi `read()` retourne des données vides

Quand `VIDIOC_STREAMON` n'est **JAMAIS** appelé :

1. ❌ `esp_cam_ctlr_start()` n'est jamais appelé
2. ❌ `esp_video_isp_start_by_csi()` n'est jamais appelé
3. ❌ `ESP_CAM_SENSOR_IOC_S_STREAM` n'est jamais appelé
4. ❌ Le sensor ne stream **AUCUNE** donnée
5. ❌ Les callbacks `on_trans_finished` ne sont **JAMAIS** appelés
6. ❌ Les buffers V4L2 ne sont **JAMAIS** remplis
7. ✓ `read()` retourne des zéros (ou données aléatoires)

## Séquence de Démarrage du Streaming

### Quand `ioctl(fd, VIDIOC_STREAMON, &type)` est appelé :

```c
// 1. esp_video_ioctl.c:62
esp_video_ioctl_streamon()
    ↓
// 2. esp_video.c:602
esp_video_start_capture(video, type)
    ↓
// 3. esp_video_csi_device.c:379
csi_video_start(video, type) {

    // A. Configure CSI controller
    esp_cam_ctlr_csi_config_t csi_config = {
        .h_res = 1280,
        .v_res = 720,
        .data_lane_num = 2,
        .input_data_color_type = CAM_CTLR_COLOR_RAW10,
        .output_data_color_type = CAM_CTLR_COLOR_RGB565,
        .lane_bit_rate_mbps = 1000,
    };
    esp_cam_new_csi_ctlr(&csi_config, &cam_ctrl_handle);

    // B. Enregistre callbacks pour remplir les buffers
    esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans = csi_video_on_get_new_trans,    // ← Donne buffer vide
        .on_trans_finished = csi_video_on_trans_finished,  // ← Reçoit buffer rempli
    };
    esp_cam_ctlr_register_event_callbacks(cam_ctrl_handle, &cbs, video);

    // C. Active et démarre le contrôleur CSI
    esp_cam_ctlr_enable(cam_ctrl_handle);
    esp_cam_ctlr_start(cam_ctrl_handle);

    // D. Démarre l'ISP (si nécessaire)
    esp_video_isp_start_by_csi(&csi_video->state, format);

    // E. Démarre le streaming du sensor ★★★
    int flags = 1;
    esp_cam_sensor_ioctl(sensor, ESP_CAM_SENSOR_IOC_S_STREAM, &flags);
    // ↓
    // Le sensor commence à envoyer des frames via MIPI-CSI
    // ↓
    // Le contrôleur CSI reçoit les frames
    // ↓
    // L'ISP traite les frames (RAW10 → RGB565)
    // ↓
    // Callback on_trans_finished() appelé avec buffer rempli
    // ↓
    // Buffer disponible via VIDIOC_DQBUF ou read()
}
```

## Callbacks de Remplissage des Buffers

### `csi_video_on_get_new_trans()` (esp_video_csi_device.c:274)

```c
// Appelé quand le driver CSI a besoin d'un buffer VIDE pour capturer
static bool IRAM_ATTR csi_video_on_get_new_trans(
    esp_cam_ctlr_handle_t handle,
    esp_cam_ctlr_trans_t *trans,
    void *user_data
) {
    struct esp_video *video = (struct esp_video *)user_data;

    // Obtient un buffer vide de la queue V4L2
    element = CAPTURE_VIDEO_GET_QUEUED_ELEMENT(video);

    // Donne le buffer au driver CSI pour qu'il le remplisse
    trans->buffer = element->buffer;
    trans->buflen = ELEMENT_SIZE(element);

    return true;
}
```

### `csi_video_on_trans_finished()` (esp_video_csi_device.c:239)

```c
// Appelé quand le driver CSI a REMPLI un buffer
static bool IRAM_ATTR csi_video_on_trans_finished(
    esp_cam_ctlr_handle_t handle,
    esp_cam_ctlr_trans_t *trans,
    void *user_data
) {
    struct esp_video *video = (struct esp_video *)user_data;

    ESP_EARLY_LOGD(TAG, "size=%zu", trans->received_size);  // ← Taille reçue

    // Marque le buffer comme DONE (prêt à être lu)
    CAPTURE_VIDEO_DONE_BUF(video, trans->buffer, trans->received_size);

    return true;
}
```

**Sans `VIDIOC_STREAMON`, ces callbacks ne sont JAMAIS enregistrés ni appelés !**

## Points de Défaillance pour Frames Noires

### 1. ❌ Streaming Jamais Démarré (ACTUEL)

```cpp
// mipi_dsi_cam.cpp
int fd = open(dev, O_RDWR);
ssize_t bytes_read = read(fd, buffer, size);  // ❌ Retourne zéros
close(fd);
```

**Symptôme:** Frames complètement noires (tous les pixels à 0)

### 2. ❌ ISP Non Configuré

```cpp
// esp_video_csi_device.c:423
ESP_GOTO_ON_ERROR(
    esp_video_isp_start_by_csi(&csi_video->state, format),
    exit_3, TAG, "failed to start ISP"
);
```

Si l'ISP n'est pas compilé (`enable_isp: false`) mais que `bypass_isp=false` :
- `esp_video_isp_start_by_csi()` retourne `ESP_ERR_NOT_SUPPORTED`
- Le streaming échoue complètement

**Vérification dans les logs :**
```
[esp_video_csi] failed to start ISP
```

### 3. ❌ Sensor Ne Stream Pas

```cpp
// esp_video_csi_device.c:427
int flags = 1;
ESP_GOTO_ON_ERROR(
    esp_cam_sensor_ioctl(sensor, ESP_CAM_SENSOR_IOC_S_STREAM, &flags),
    exit_4, TAG, "failed to start sensor stream"
);
```

Si le sensor ne démarre pas le streaming :
- Pas de données MIPI-CSI
- Callbacks jamais appelés
- Buffers vides

**Vérification dans les logs :**
```
[esp_video_csi] failed to start sensor stream
```

### 4. ❌ Format/Résolution Incorrects

Si la résolution demandée ne correspond pas à celle configurée dans le sensor :
- Le sensor peut streamer des données invalides
- L'ISP peut mal interpréter les données
- Frames corrompues ou noires

### 5. ❌ XCLK Non Initialisé (DÉJÀ RÉSOLU)

Si XCLK n'est pas initialisé :
- Le sensor ne répond pas sur I2C (PID=0x0)
- `/dev/video0` n'est pas créé
- Impossible d'ouvrir le device

**Ce problème a été résolu dans XCLK_MIPI_CSI_FIX.md**

## ✅ Solution : Implémenter le Flux V4L2 Complet

### Option A : Utiliser VIDIOC_STREAMON/DQBUF (Recommandé)

```cpp
bool MipiDSICamComponent::capture_snapshot_to_file(const std::string &path) {
    const char *dev = wants_jpeg_(this->pixel_format_) ?
                      ESP_VIDEO_JPEG_DEVICE_NAME :
                      ESP_VIDEO_ISP1_DEVICE_NAME;

    // 1. Ouvrir le device
    int fd = open(dev, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        ESP_LOGE(TAG, "open(%s) failed: %s", dev, strerror(errno));
        return false;
    }

    // 2. Demander des buffers
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 2;  // 2 buffers
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        ESP_LOGE(TAG, "VIDIOC_REQBUFS failed: %s", strerror(errno));
        close(fd);
        return false;
    }

    // 3. Mapper et queue les buffers
    struct {
        void *start;
        size_t length;
    } buffers[2];

    for (int i = 0; i < 2; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        // Obtenir info du buffer
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            ESP_LOGE(TAG, "VIDIOC_QUERYBUF failed: %s", strerror(errno));
            close(fd);
            return false;
        }

        // Mapper le buffer
        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, buf.m.offset);

        if (buffers[i].start == MAP_FAILED) {
            ESP_LOGE(TAG, "mmap failed: %s", strerror(errno));
            close(fd);
            return false;
        }

        // Queue le buffer
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF failed: %s", strerror(errno));
            close(fd);
            return false;
        }
    }

    // 4. Démarrer le streaming ★★★
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        ESP_LOGE(TAG, "VIDIOC_STREAMON failed: %s", strerror(errno));
        close(fd);
        return false;
    }

    ESP_LOGI(TAG, "Streaming démarré, attente d'une frame...");

    // 5. Capturer une frame
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
        ESP_LOGE(TAG, "VIDIOC_DQBUF failed: %s", strerror(errno));
        ioctl(fd, VIDIOC_STREAMOFF, &type);
        close(fd);
        return false;
    }

    ESP_LOGI(TAG, "Frame capturée: %u bytes", buf.bytesused);

    // 6. Sauvegarder la frame
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) {
        ESP_LOGE(TAG, "fopen(%s) failed: %s", path.c_str(), strerror(errno));
        ioctl(fd, VIDIOC_STREAMOFF, &type);
        close(fd);
        return false;
    }

    size_t written = fwrite(buffers[buf.index].start, 1, buf.bytesused, f);
    fclose(f);

    // 7. Arrêter le streaming
    ioctl(fd, VIDIOC_STREAMOFF, &type);

    // 8. Libérer les buffers
    for (int i = 0; i < 2; i++) {
        munmap(buffers[i].start, buffers[i].length);
    }

    close(fd);

    ESP_LOGI(TAG, "✅ Snapshot sauvegardé: %s (%u bytes)",
             path.c_str(), (unsigned)written);

    return (written == buf.bytesused);
}
```

### Option B : Utiliser le Device ISP Directement avec read() (Plus Simple)

**IMPORTANT:** `read()` ne fonctionne QUE si le streaming a déjà été démarré par ailleurs, OU si le device supporte V4L2_CAP_READWRITE.

```cpp
bool MipiDSICamComponent::capture_snapshot_to_file_simple(const std::string &path) {
    // Utiliser /dev/video20 (ISP) au lieu de /dev/video0 (CSI)
    // L'ISP peut avoir un mode read() direct
    const char *dev = ESP_VIDEO_ISP1_DEVICE_NAME;  // /dev/video20

    int fd = open(dev, O_RDONLY);
    if (fd < 0) {
        ESP_LOGE(TAG, "open(%s) failed: %s", dev, strerror(errno));
        return false;
    }

    // Vérifier les capacités
    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        ESP_LOGE(TAG, "VIDIOC_QUERYCAP failed");
        close(fd);
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
        ESP_LOGE(TAG, "Device does not support read()");
        close(fd);
        return false;
    }

    // read() peut maintenant fonctionner
    uint8_t *buffer = (uint8_t*)heap_caps_malloc(MAX_FRAME_SIZE, MALLOC_CAP_8BIT);
    ssize_t bytes_read = read(fd, buffer, MAX_FRAME_SIZE);

    // ... reste du code
}
```

## Vérifications de Diagnostic

### 1. Vérifier que le Streaming est Supporté

```cpp
struct v4l2_capability cap;
ioctl(fd, VIDIOC_QUERYCAP, &cap);

if (cap.capabilities & V4L2_CAP_STREAMING) {
    ESP_LOGI(TAG, "✓ STREAMING supporté");
} else {
    ESP_LOGE(TAG, "✗ STREAMING non supporté");
}

if (cap.capabilities & V4L2_CAP_READWRITE) {
    ESP_LOGI(TAG, "✓ READWRITE supporté");
} else {
    ESP_LOGE(TAG, "✗ READWRITE non supporté");
}
```

### 2. Vérifier le Format Actuel

```cpp
struct v4l2_format fmt;
memset(&fmt, 0, sizeof(fmt));
fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

if (ioctl(fd, VIDIOC_G_FMT, &fmt) == 0) {
    ESP_LOGI(TAG, "Format actuel:");
    ESP_LOGI(TAG, "  Largeur: %u", fmt.fmt.pix.width);
    ESP_LOGI(TAG, "  Hauteur: %u", fmt.fmt.pix.height);
    ESP_LOGI(TAG, "  PixelFormat: 0x%08X", fmt.fmt.pix.pixelformat);
    ESP_LOGI(TAG, "  Taille image: %u", fmt.fmt.pix.sizeimage);
}
```

### 3. Ajouter Logs dans csi_video_start()

Modifier `components/esp_video/src/device/esp_video_csi_device.c:379` :

```c
static esp_err_t csi_video_start(struct esp_video *video, uint32_t type)
{
    ESP_LOGI(TAG, "=== CSI VIDEO START ===");
    ESP_LOGI(TAG, "Resolution: %ux%u",
             CAPTURE_VIDEO_GET_FORMAT_WIDTH(video),
             CAPTURE_VIDEO_GET_FORMAT_HEIGHT(video));

    // ... code existant ...

    ESP_GOTO_ON_ERROR(esp_cam_ctlr_start(csi_video->cam_ctrl_handle), exit_2, TAG,
                      "failed to start CAM ctlr");
    ESP_LOGI(TAG, "✓ CSI controller started");

    ESP_GOTO_ON_ERROR(esp_video_isp_start_by_csi(&csi_video->state,
                      STREAM_FORMAT(CAPTURE_VIDEO_STREAM(video))),
                      exit_3, TAG, "failed to start ISP");
    ESP_LOGI(TAG, "✓ ISP started");

    int flags = 1;
    ESP_GOTO_ON_ERROR(esp_cam_sensor_ioctl(csi_video->cam.sensor,
                      ESP_CAM_SENSOR_IOC_S_STREAM, &flags),
                      exit_4, TAG, "failed to start sensor stream");
    ESP_LOGI(TAG, "✓ Sensor streaming started");

    ESP_LOGI(TAG, "=== CSI VIDEO START SUCCESS ===");
    return ESP_OK;

    // ... gestion d'erreurs ...
}
```

## Résumé

### Cause Racine

**Le code actuel appelle `read()` sur `/dev/video0` SANS jamais appeler `VIDIOC_STREAMON`.**

Conséquences :
1. Le contrôleur CSI n'est jamais démarré
2. L'ISP n'est jamais démarré
3. Le sensor ne stream jamais de données
4. Les buffers ne sont jamais remplis
5. `read()` retourne des zéros (frame noire)

### Solution

Implémenter le flux V4L2 complet :
1. `VIDIOC_REQBUFS` - Allouer des buffers
2. `VIDIOC_QUERYBUF` + `mmap()` - Mapper les buffers
3. `VIDIOC_QBUF` - Mettre les buffers dans la queue
4. **`VIDIOC_STREAMON`** - Démarrer le streaming ★★★
5. `VIDIOC_DQBUF` - Récupérer une frame
6. Traiter/sauvegarder la frame
7. `VIDIOC_STREAMOFF` - Arrêter le streaming

### Prochaines Étapes

1. ✅ Implémenter la fonction V4L2 complète dans `mipi_dsi_cam.cpp`
2. ✅ Tester la capture avec logs de diagnostic
3. ✅ Vérifier que les callbacks sont appelés
4. ✅ Valider que les frames contiennent des données réelles

## Références

- **Exemple de référence:** `components/esp_video/exemples/capture_stream/main/capture_stream_main.c`
- **Documentation V4L2:** https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/v4l2.html
- **ESP-IDF CSI Driver:** Components esp_video sources
