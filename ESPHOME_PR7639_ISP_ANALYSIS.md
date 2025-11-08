# Analyse ESPHome PR#7639: Contrôle ISP Direct pour Image Claire et Nette

## Découverte Majeure: Contrôle CCM Direct!

La PR ESPHome #7639 obtient des images claires et nettes en utilisant **l'API ISP d'ESP-IDF directement**, pas V4L2!

### Architecture Fondamentale

**ESPHome PR#7639:**
```
SC202CS → esp_cam_sensor API → ISP (contrôle direct) → Pipeline ESPHome
                                   ↑
                    esp_isp_ccm_configure()
                    esp_isp_color_configure()
```

**Notre implémentation actuelle:**
```
SC202CS → V4L2 (/dev/video0) → ISP (abstrait) → MMAP buffers → LVGL
                                   ↑
                      Pas d'accès direct aux contrôles ISP
```

---

## Code Clé: Color Correction Matrix (CCM)

### Gains RGB Séparés - LA SOLUTION au Blanc→Vert!

```cpp
void ISP::configure_color_correction_() {
  esp_isp_ccm_config_t config = {
    .matrix = {{this->red_,   0.0f,         0.0f},      // Gain canal rouge
               {0.0f,         this->green_, 0.0f},      // Gain canal vert
               {0.0f,         0.0f,         this->blue_}}, // Gain canal bleu
    .saturation = true
  };

  esp_isp_ccm_disable(this->isp_proc_handle_);

  if (esp_isp_ccm_configure(this->isp_proc_handle_, &config) != ESP_OK)
    ESP_LOGE(TAG, "esp_isp_ccm_configure failed.");

  if (esp_isp_ccm_enable(this->isp_proc_handle_) != ESP_OK)
    ESP_LOGE(TAG, "esp_isp_ccm_enable failed.");
}

// Contrôles exposés:
void ISP::number_red(float value) {
  this->red_ = value;
  this->configure_color_correction_();
}

void ISP::number_green(float value) {
  this->green_ = value;
  this->configure_color_correction_();
}

void ISP::number_blue(float value) {
  this->blue_ = value;
  this->configure_color_correction_();
}
```

**Comment corriger blanc→vert:**
- Si blanc apparaît vert → réduire `green_` (ex: 0.8) et augmenter `red_`/`blue_` (ex: 1.2)
- Ajustement typique: `red=1.5, green=1.0, blue=1.6` (selon PR description)

---

## Code Clé: Contrôles ISP Couleur

### Brightness, Contrast, Saturation, Hue

```cpp
void ISP::configure_color_() {
  esp_isp_color_config_t config = {
    .color_contrast = {
      .decimal = this->contrast_ < 128 ? this->contrast_ : 0,
      .integer = this->contrast_ >= 128 ? 1 : 0
    },
    .color_saturation = {
      .decimal = this->saturation_ < 128 ? this->saturation_ : 0,
      .integer = this->saturation_ >= 128 ? 1 : 0
    },
    .color_hue = this->hue_,
    .color_brightness = this->brightness_
  };

  if (esp_isp_color_configure(this->isp_proc_handle_, &config) != ESP_OK)
    ESP_LOGE(TAG, "esp_isp_color_configure failed.");

  esp_isp_color_enable(this->isp_proc_handle_);
}

// Contrôles exposés:
void ISP::number_brightness(float value) {
  this->brightness_ = static_cast<int8_t>(value);
  this->configure_color_();
}

void ISP::number_contrast(float value) {
  this->contrast_ = static_cast<uint8_t>(value);
  this->configure_color_();
}

void ISP::number_saturation(float value) {
  this->saturation_ = static_cast<uint8_t>(value);
  this->configure_color_();
}

void ISP::number_hue(float value) {
  this->hue_ = static_cast<uint16_t>(value);
  this->configure_color_();
}
```

---

## Code Clé: Contrôle Capteur (Exposition/Gain)

### Via esp_cam_sensor API (PAS V4L2!)

```cpp
// Gain capteur
if (esp_cam_sensor_set_para_value(this->sensor_handle_,
                                   "ESP_CAM_SENSOR_GAIN",
                                   this->gain_,
                                   true) != ESP_OK) {
  ESP_LOGE(TAG, "esp_cam_sensor_set_para_value gain failed.");
}

// Exposition capteur
if (esp_cam_sensor_set_para_value(this->sensor_handle_,
                                   "ESP_CAM_SENSOR_EXPOSURE_VAL",
                                   this->exposure_,
                                   true) != ESP_OK) {
  ESP_LOGE(TAG, "esp_cam_sensor_set_para_value exposure failed.");
}
```

---

## Comparaison: ESPHome PR#7639 vs Notre Implémentation

| Aspect | ESPHome PR#7639 | Notre Implémentation V4L2 | Gagnant |
|--------|-----------------|---------------------------|---------|
| **API utilisée** | `esp_cam_sensor` + `esp_isp` | V4L2 (`ioctl`) | PR#7639 (plus direct) |
| **Contrôle CCM RGB** | ✅ `esp_isp_ccm_configure()` | ❌ Non disponible via V4L2 | **PR#7639** |
| **Brightness** | ✅ `esp_isp_color_configure()` | ❌ Non disponible | **PR#7639** |
| **Contrast** | ✅ `esp_isp_color_configure()` | ❌ Non disponible | **PR#7639** |
| **Saturation** | ✅ `esp_isp_color_configure()` | ❌ Non disponible | **PR#7639** |
| **Hue** | ✅ `esp_isp_color_configure()` | ❌ Non disponible | **PR#7639** |
| **Exposition** | ✅ `esp_cam_sensor` API | ✅ V4L2_CID_EXPOSURE_ABSOLUTE | Égal |
| **Gain** | ✅ `esp_cam_sensor` API | ✅ V4L2_CID_GAIN | Égal |
| **WB Temperature** | ⚠️ Non montré | ✅ V4L2_CID_WHITE_BALANCE_TEMPERATURE | **Nous** |
| **Correction blanc→vert** | ✅ Via CCM RGB gains | ❌ Limité à WB temp | **PR#7639** |
| **Zero-copy** | ⚠️ Non clair | ✅ Oui (MMAP direct) | **Nous** |
| **Latence** | ⚠️ Non documenté | ✅ 0.45ms | **Nous** |

---

## Pourquoi Leur Approche Fonctionne Mieux pour les Couleurs

### 1. Accès Direct à la CCM (Color Correction Matrix)

**Matrice CCM standard (3x3):**
```
    R_out     |R_r  R_g  R_b|   R_in
    G_out  =  |G_r  G_g  G_b| × G_in
    B_out     |B_r  B_g  B_b|   B_in
```

**ESPHome PR#7639 (matrice diagonale simple):**
```cpp
.matrix = {{red,   0.0,   0.0},
           {0.0,   green, 0.0},
           {0.0,   0.0,   blue}}
```

**Effet:**
- `red=1.5`   → Rouge multiplié par 1.5 (plus rouge)
- `green=1.0` → Vert normal (référence)
- `blue=1.6`  → Bleu multiplié par 1.6 (plus bleu)

**Pour corriger blanc→vert:**
- Blanc apparaît vert → trop de vert dans la sortie
- Solution: réduire gain vert, augmenter rouge/bleu
- Exemple: `red=1.3, green=0.85, blue=1.25`

### 2. Contrôles ISP Supplémentaires

**Brightness (+/- luminosité globale):**
```cpp
.color_brightness = this->brightness_  // -128 à +127
```

**Saturation (intensité couleurs):**
```cpp
.color_saturation = {
  .decimal = this->saturation_ < 128 ? this->saturation_ : 0,
  .integer = this->saturation_ >= 128 ? 1 : 0
}
```

**Hue (rotation teinte):**
```cpp
.color_hue = this->hue_  // 0-360 degrés
```

Ces contrôles permettent d'affiner l'image APRÈS la CCM.

---

## Limitations de Notre Approche V4L2

### V4L2 = Abstraction Linux sur ISP

```
┌─────────────┐
│  V4L2 API   │ ← Notre code (ioctl)
├─────────────┤
│  Kernel     │
│  Driver     │
├─────────────┤
│  ISP ESP32  │ ← Contrôles CCM/Color ici, mais pas exposés par V4L2!
└─────────────┘
```

**Contrôles V4L2 disponibles (via CID):**
- ✅ V4L2_CID_EXPOSURE_ABSOLUTE (exposition)
- ✅ V4L2_CID_GAIN (gain capteur)
- ✅ V4L2_CID_AUTO_WHITE_BALANCE (mode AWB)
- ✅ V4L2_CID_WHITE_BALANCE_TEMPERATURE (température couleur)

**Contrôles V4L2 NON disponibles:**
- ❌ CCM RGB gains individuels
- ❌ ISP brightness
- ❌ ISP contrast
- ❌ ISP saturation
- ❌ ISP hue

**Raison:** Le driver V4L2 ESP32-P4 n'expose PAS ces contrôles ISP de bas niveau!

---

## Solutions Possibles pour Notre Code

### Option 1: Ajouter Accès ISP Direct (Hybride)

**Combiner V4L2 + API ISP:**

```cpp
// Dans mipi_dsi_cam.cpp

#include "esp_isp.h"
#include "esp_isp_ccm.h"
#include "esp_isp_color.h"

class MipiDSICamComponent {
  private:
    isp_proc_handle_t isp_handle_ = nullptr;  // Handle ISP

  public:
    // Nouvelles méthodes
    bool set_rgb_gains(float red, float green, float blue);
    bool set_brightness(int8_t value);
    bool set_contrast(uint8_t value);
    bool set_saturation(uint8_t value);
    bool set_hue(uint16_t value);
};

bool MipiDSICamComponent::set_rgb_gains(float red, float green, float blue) {
  if (!this->isp_handle_) {
    ESP_LOGE(TAG, "ISP handle not initialized");
    return false;
  }

  esp_isp_ccm_config_t config = {
    .matrix = {{red,  0.0f, 0.0f},
               {0.0f, green, 0.0f},
               {0.0f, 0.0f,  blue}},
    .saturation = true
  };

  esp_isp_ccm_disable(this->isp_handle_);

  if (esp_isp_ccm_configure(this->isp_handle_, &config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure CCM");
    return false;
  }

  if (esp_isp_ccm_enable(this->isp_handle_) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable CCM");
    return false;
  }

  ESP_LOGI(TAG, "✓ CCM RGB gains: R=%.2f, G=%.2f, B=%.2f", red, green, blue);
  return true;
}

bool MipiDSICamComponent::set_brightness(int8_t value) {
  // Similaire avec esp_isp_color_configure()
  // ...
}
```

**Problème potentiel:**
- Besoin de récupérer le `isp_proc_handle_` depuis V4L2 ou pipeline vidéo
- Peut-être pas accessible facilement depuis notre composant

### Option 2: Modifier le Driver V4L2 ESP32

**Ajouter CID personnalisés au driver V4L2:**

```c
// Dans driver V4L2 ESP32 (kernel)
#define V4L2_CID_USER_ESP32_CCM_RED    (V4L2_CID_USER_BASE + 0x1000)
#define V4L2_CID_USER_ESP32_CCM_GREEN  (V4L2_CID_USER_BASE + 0x1001)
#define V4L2_CID_USER_ESP32_CCM_BLUE   (V4L2_CID_USER_BASE + 0x1002)
#define V4L2_CID_USER_ESP32_BRIGHTNESS (V4L2_CID_USER_BASE + 0x1003)
// etc...
```

**Avantage:** Garde approche V4L2 standard
**Inconvénient:** Nécessite modification du kernel ESP-IDF

### Option 3: Utiliser esp_cam_sensor + esp_isp comme PR#7639

**Abandonner V4L2, utiliser APIs Espressif directement:**

**Avantages:**
- ✅ Accès complet CCM RGB gains
- ✅ Contrôles ISP brightness/contrast/saturation/hue
- ✅ API native ESP-IDF (plus direct)

**Inconvénients:**
- ❌ Perdre zero-copy MMAP (dépend de l'implémentation)
- ❌ Réécriture complète du code
- ❌ Plus dépendant d'Espressif (moins portable)

---

## Recommandation: Approche Hybride Court Terme

### 1. Garder V4L2 pour Exposition/Gain

Notre code actuel fonctionne bien:
```cpp
set_exposure(10000);  // V4L2_CID_EXPOSURE_ABSOLUTE
set_gain(16000);      // V4L2_CID_GAIN
```

### 2. Ajouter API ISP pour CCM RGB

Nouveau code à ajouter:
```cpp
// Corriger blanc→vert avec CCM
id(tab5_cam).set_rgb_gains(1.3, 0.85, 1.25);  // esp_isp_ccm_configure()

// Ajuster luminosité si nécessaire
id(tab5_cam).set_brightness(20);  // esp_isp_color_configure()
```

### 3. Conserver Zero-Copy MMAP

L'approche zero-copy reste valide et plus rapide que toute copie PPA/buffer.

---

## Code Exemple: Implémentation Hybride

```cpp
// mipi_dsi_cam.h
class MipiDSICamComponent : public Component {
  public:
    // V4L2 controls (existants)
    bool set_exposure(int value);
    bool set_gain(int value);
    bool set_white_balance_mode(bool auto_mode);
    bool set_white_balance_temp(int kelvin);

    // ISP controls (nouveaux)
    bool set_rgb_gains(float red, float green, float blue);
    bool set_brightness(int8_t value);  // -128 à +127
    bool set_contrast(uint8_t value);   // 0 à 255
    bool set_saturation(uint8_t value); // 0 à 255

  private:
    isp_proc_handle_t isp_handle_ = nullptr;

    // Valeurs CCM actuelles
    float ccm_red_ = 1.0f;
    float ccm_green_ = 1.0f;
    float ccm_blue_ = 1.0f;

    // Valeurs color actuelles
    int8_t brightness_ = 0;
    uint8_t contrast_ = 128;
    uint8_t saturation_ = 128;
};
```

---

## Valeurs Typiques pour Correction Blanc→Vert

**Basé sur PR #7639 description ("red: 1.5, green: 1.0, blue: 1.6"):**

```cpp
// Configuration de départ (blanc → vert)
id(tab5_cam).set_rgb_gains(1.0, 1.0, 1.0);  // Tous égaux → problème persiste

// Correction légère
id(tab5_cam).set_rgb_gains(1.2, 0.9, 1.15);  // Réduire vert, augmenter R/B

// Correction moyenne
id(tab5_cam).set_rgb_gains(1.3, 0.85, 1.25);  // Plus d'ajustement

// Correction forte (comme PR #7639)
id(tab5_cam).set_rgb_gains(1.5, 1.0, 1.6);  // Configuration M5Stack

// Ajustement additionnel luminosité si besoin
id(tab5_cam).set_brightness(10);  // Augmenter légèrement
id(tab5_cam).set_saturation(140); // Plus de saturation (couleurs plus vives)
```

---

## Conclusion

**Pourquoi ESPHome PR#7639 a des images claires et nettes:**

1. ✅ **Contrôle CCM RGB direct** → Corrige blanc→vert parfaitement
2. ✅ **Contrôles ISP brightness/contrast/saturation** → Affinage image
3. ✅ **API esp_cam_sensor + esp_isp native** → Accès bas niveau complet

**Notre implémentation actuelle:**

✅ **Points forts:**
- Zero-copy ultra rapide (0.45ms vs 15-20ms M5Stack PPA)
- Contrôles V4L2 exposition/gain/WB fonctionnels
- Architecture simple et efficace

❌ **Limitation:**
- Pas d'accès CCM RGB → Correction blanc→vert limitée
- Pas d'accès ISP brightness/contrast/saturation

**Action recommandée:**
- Implémenter approche hybride: V4L2 + ISP API
- Ajouter `set_rgb_gains()` pour correction CCM
- Ajouter contrôles ISP brightness/contrast si nécessaire
- Garder zero-copy MMAP (avantage performance)

**Alternative long terme:**
- Contribuer au driver V4L2 ESP32 pour exposer contrôles ISP
- Ou migrer vers architecture modulaire comme PR#7639 (réécriture complète)
