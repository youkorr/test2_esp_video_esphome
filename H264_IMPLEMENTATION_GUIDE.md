# Guide d'implémentation H.264 sur ESP32-P4

## 🎯 Vue d'ensemble

L'ESP32-P4 dispose d'un **encodeur H.264 hardware** qui peut encoder des vidéos en temps réel. Votre projet a déjà le composant `esp_h264` intégré et l'infrastructure est en place dans `mipi_dsi_cam`.

## 📊 Architecture Vidéo

```
┌─────────────┐    RAW8     ┌──────────┐    RGB565/YUV420    ┌──────────────┐
│  SC202CS    │ ──────────> │   ISP    │ ─────────────────> │   Output     │
│  Sensor     │  MIPI CSI   │/dev/video│                     │              │
└─────────────┘             │   20     │                     └──────────────┘
                            └──────────┘                              │
                                                                      │
                          ┌──────────────────────────────────────────┤
                          │                                          │
                          v                                          v
                    ┌───────────┐                              ┌───────────┐
                    │   JPEG    │                              │   H.264   │
                    │ /dev/video│                              │/dev/video │
                    │    10     │                              │    11     │
                    └───────────┘                              └───────────┘
```

## 🔑 Points Clés

### 1. Formats de Pixels

| Format  | Usage                    | Devices                |
|---------|--------------------------|------------------------|
| RAW8    | Sensor → ISP             | Caméra MIPI CSI        |
| RGB565  | Display LVGL             | ISP → Display          |
| YUV420  | H.264 Encoder (REQUIS)   | ISP → H.264 encoder    |
| YUYV    | Alternative YUV422       | ISP → JPEG/H.264       |

### 2. Pipeline H.264

**Problème actuel** : Votre système capture en **RGB565** pour LVGL display.
**Pour H.264** : Il faut capturer en **YUV420** (format requis par H.264 hardware encoder).

**Solutions possibles** :

#### Option A : Dual Pipeline (RECOMMANDÉ)
- Pipeline 1 : RAW8 → ISP → RGB565 → LVGL Display (30 FPS)
- Pipeline 2 : RAW8 → ISP → YUV420 → H.264 Encoder → Stream (30 FPS)

**Avantages** :
- Display et streaming simultanés
- Performance optimale (2 pipelines indépendants)

**Inconvénients** :
- Plus complexe à implémenter
- Consommation mémoire doublée

#### Option B : Single Pipeline avec conversion
- Pipeline : RAW8 → ISP → YUV420 → Display + H.264

**Avantages** :
- Simple à implémenter
- Une seule capture

**Inconvénients** :
- Nécessite conversion YUV420 → RGB565 pour display (coût CPU)
- Ou utiliser LVGL avec format YUV (support limité)

#### Option C : Alternance Display/Stream
- Frame paire : RGB565 → Display
- Frame impaire : YUV420 → H.264

**Avantages** :
- Compromis ressources/performance
- Pas de duplication de pipeline

**Inconvénients** :
- FPS effectif réduit de moitié (15 FPS display, 15 FPS stream)

## 🚀 Implémentation Recommandée (Option A)

### Étape 1 : Configuration YAML

```yaml
# Dans votre fichier ESPHome YAML
mipi_dsi_cam:
  id: main_camera
  i2c_id: bsp_bus
  sensor_type: sc202cs
  resolution: "1280x720"
  pixel_format: "YUV420"  # ← Changer de RGB565 à YUV420
  framerate: 30

# Nouveau composant pour streaming H.264
h264_stream:
  id: h264_streamer
  camera_id: main_camera
  bitrate: 2000000  # 2 Mbps
  gop: 30
  quality: 25  # QP (Quantization Parameter) : 0=meilleur, 51=pire

  # Sortie stream (exemples)
  output:
    - http_server:  # Servir via HTTP
        port: 8080
        path: "/stream.h264"
    - mqtt:  # Publier via MQTT
        topic: "esphome/camera/h264"
    - websocket:  # WebSocket pour navigateur
        port: 8081
```

### Étape 2 : Créer le composant `h264_stream`

Il faut créer un nouveau composant ESPHome pour gérer le streaming H.264 :

**Fichier : `components/h264_stream/h264_stream.h`**

```cpp
#pragma once

#include "esphome/core/component.h"
#include "esphome/components/mipi_dsi_cam/mipi_dsi_cam.h"
#include "esp_h264_enc_single.h"

namespace esphome {
namespace h264_stream {

class H264StreamComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_camera(mipi_dsi_cam::MipiDSICamComponent *camera) { camera_ = camera; }
  void set_bitrate(uint32_t bitrate) { bitrate_ = bitrate; }
  void set_gop(uint8_t gop) { gop_ = gop; }
  void set_quality(uint8_t quality) { quality_ = quality; }

  // Callbacks pour différents outputs
  void add_data_callback(std::function<void(const uint8_t*, size_t)> callback);

 protected:
  mipi_dsi_cam::MipiDSICamComponent *camera_{nullptr};

  esp_h264_enc_handle_t h264_encoder_{nullptr};
  uint8_t *encoded_buffer_{nullptr};
  size_t encoded_buffer_size_{0};

  uint32_t bitrate_{2000000};
  uint8_t gop_{30};
  uint8_t quality_{25};

  std::vector<std::function<void(const uint8_t*, size_t)>> data_callbacks_;

  bool encode_frame_();
};

}  // namespace h264_stream
}  // namespace esphome
```

**Fichier : `components/h264_stream/h264_stream.cpp`**

```cpp
#include "h264_stream.h"
#include "esphome/core/log.h"

namespace esphome {
namespace h264_stream {

static const char *const TAG = "h264_stream";

void H264StreamComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up H.264 Stream...");

  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "Camera not configured");
    this->mark_failed();
    return;
  }

  // Configurer l'encodeur H.264
  esp_h264_enc_cfg_hw_t encoder_cfg = {
    .width = this->camera_->get_image_width(),
    .height = this->camera_->get_image_height(),
    .src_rate = 30,  // FPS source
    .dst_rate = 30,  // FPS encodé
    .gop = this->gop_,
    .qp_max = this->quality_ + 1,
    .qp_min = this->quality_,
    .bitrate = this->bitrate_,
    .input_format = ESP_H264_RAW_FMT_O_UYY_E_VYY,  // YUV420
  };

  esp_h264_err_t ret = esp_h264_enc_open(&encoder_cfg, &this->h264_encoder_);
  if (ret != ESP_H264_ERR_OK) {
    ESP_LOGE(TAG, "Failed to open H.264 encoder: %d", ret);
    this->mark_failed();
    return;
  }

  // Allouer buffer de sortie H.264
  this->encoded_buffer_size_ = encoder_cfg.width * encoder_cfg.height / 2;  // Estimation
  this->encoded_buffer_ = (uint8_t*)heap_caps_malloc(
    this->encoded_buffer_size_,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
  );

  if (this->encoded_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate H.264 output buffer");
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "✅ H.264 Stream initialized");
  ESP_LOGI(TAG, "   Resolution: %ux%u", encoder_cfg.width, encoder_cfg.height);
  ESP_LOGI(TAG, "   Bitrate: %u bps (%.2f Mbps)", this->bitrate_, this->bitrate_ / 1000000.0f);
  ESP_LOGI(TAG, "   GOP: %u frames", this->gop_);
  ESP_LOGI(TAG, "   Quality (QP): %u", this->quality_);
}

void H264StreamComponent::loop() {
  if (!this->camera_->is_streaming()) {
    return;
  }

  // Encoder une frame
  if (this->encode_frame_()) {
    // Notifier tous les callbacks (HTTP, MQTT, WebSocket, etc.)
    for (auto &callback : this->data_callbacks_) {
      callback(this->encoded_buffer_, this->encoded_buffer_size_);
    }
  }
}

bool H264StreamComponent::encode_frame_() {
  // Récupérer frame YUV420 depuis la caméra
  mipi_dsi_cam::SimpleBufferElement *buffer = nullptr;
  uint8_t *yuv_data = nullptr;
  int width, height;

  if (!this->camera_->get_current_rgb_frame(&buffer, &yuv_data, &width, &height)) {
    return false;
  }

  // Encoder la frame
  esp_h264_enc_in_frame_t in_frame = {
    .raw_data = {
      .buffer = yuv_data,
      .len = width * height * 3 / 2,  // YUV420 = 1.5 bytes/pixel
    }
  };

  esp_h264_enc_out_frame_t out_frame = {
    .raw_data = {
      .buffer = this->encoded_buffer_,
      .len = this->encoded_buffer_size_,
    }
  };

  esp_h264_err_t ret = esp_h264_enc_process(this->h264_encoder_, &in_frame, &out_frame);

  // Libérer le buffer caméra
  this->camera_->release_buffer(buffer);

  if (ret != ESP_H264_ERR_OK) {
    ESP_LOGW(TAG, "H.264 encoding failed: %d", ret);
    return false;
  }

  this->encoded_buffer_size_ = out_frame.length;
  return true;
}

void H264StreamComponent::add_data_callback(std::function<void(const uint8_t*, size_t)> callback) {
  this->data_callbacks_.push_back(callback);
}

void H264StreamComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "H.264 Stream:");
  ESP_LOGCONFIG(TAG, "  Resolution: %ux%u",
    this->camera_->get_image_width(),
    this->camera_->get_image_height());
  ESP_LOGCONFIG(TAG, "  Bitrate: %u bps", this->bitrate_);
  ESP_LOGCONFIG(TAG, "  GOP: %u", this->gop_);
  ESP_LOGCONFIG(TAG, "  Quality: %u", this->quality_);
}

}  // namespace h264_stream
}  // namespace esphome
```

## 📝 Configuration Complète

### YAML Example

```yaml
# Configuration complète avec H.264 streaming
esphome:
  name: esp32-p4-camera

esp32:
  board: esp32-p4-function-ev-board
  variant: esp32p4
  framework:
    type: esp-idf

# Dépendances externes
external_components:
  - source: github://your-repo/esphome-components
    components: [ mipi_dsi_cam, h264_stream, esp_video, esp_h264 ]

# Configuration caméra
i2c:
  - id: bsp_bus
    sda: GPIO8
    scl: GPIO9
    frequency: 400kHz

# Caméra MIPI avec sortie YUV420
mipi_dsi_cam:
  id: main_camera
  i2c_id: bsp_bus
  sensor_type: sc202cs
  resolution: "1280x720"
  pixel_format: "YUV420"  # ← IMPORTANT pour H.264
  framerate: 30

# Streaming H.264
h264_stream:
  id: video_stream
  camera_id: main_camera
  bitrate: 2000000  # 2 Mbps
  gop: 30
  quality: 25

# Serveur Web pour streamer H.264
web_server:
  port: 80
  auth:
    username: admin
    password: !secret wifi_password

# API pour Home Assistant
api:
  services:
    # Service pour démarrer/arrêter le streaming
    - service: start_h264_stream
      then:
        - lambda: |-
            id(video_stream).start_streaming();

    - service: stop_h264_stream
      then:
        - lambda: |-
            id(video_stream).stop_streaming();
```

## 🔧 Contrôle Exposition (Image trop claire)

Vous avez mentionné que l'image est **trop éclairée**. Ajoutez ces contrôles :

```yaml
mipi_dsi_cam:
  id: main_camera
  sensor_type: sc202cs
  resolution: "1280x720"
  pixel_format: "YUV420"

  # Contrôles d'exposition et de luminosité
  camera_controls:
    # Réduire l'exposition
    - id: V4L2_CID_EXPOSURE
      initial_value: 500  # Valeur plus basse = moins de lumière (essayez 200-800)

    # Réduire le gain
    - id: V4L2_CID_GAIN
      initial_value: 50  # Valeur plus basse = moins de gain (essayez 30-100)

    # Réduire la luminosité
    - id: V4L2_CID_BRIGHTNESS
      initial_value: 0  # Valeur négative = plus sombre (essayez -50 à +50)

    # Activer l'auto-exposition avec target plus basse
    - id: V4L2_CID_EXPOSURE_AUTO
      initial_value: 1  # 1 = auto exposure
```

## 🎯 Prochaines Étapes

1. **Tester avec RGB565 d'abord** : Vérifier que la correction FPS fonctionne (devrait passer de 4 FPS à 30 FPS)

2. **Passer à YUV420** : Changer `pixel_format: "YUV420"` dans la config

3. **Implémenter h264_stream** : Créer le composant selon le code ci-dessus

4. **Tester l'encodage** : Vérifier que H.264 encoder fonctionne

5. **Ajouter sortie stream** : HTTP, MQTT, ou WebSocket selon vos besoins

## 📚 Références

- ESP32-P4 H.264 Encoder API : `components/esp_h264/interface/include/`
- ESP Video Framework : `components/esp_video/include/`
- JPEG Encoder (similaire) : `components/esp_video/src/device/esp_video_jpeg_device.c`
- Exemples : `components/esp_video/exemples/`

## ⚠️ Notes Importantes

1. **Format YUV420 pour H.264** : L'encodeur hardware ESP32-P4 nécessite YUV420 en entrée
2. **Performance** : H.264 hardware peut encoder 1280x720@30fps en temps réel
3. **Bitrate** : Ajuster selon qualité souhaitée (1-5 Mbps pour 720p)
4. **GOP (Group of Pictures)** : 30 = une I-frame toutes les 30 frames (1 seconde à 30fps)
5. **QP (Quantization)** : 25-26 = bonne qualité, 30-35 = qualité moyenne, 40+ = basse qualité
