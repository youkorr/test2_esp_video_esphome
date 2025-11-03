#pragma once

#ifdef USE_ESP32_VARIANT_ESP32P4

extern "C" {
  #include "esp_ipa_types.h"

  #include "driver/isp.h"
  #include <math.h>
}

#include <vector>
#include <algorithm>

namespace esphome {
namespace mipi_dsi_cam {

// Configuration des algorithmes IPA
struct IPAConfig {
  // Auto White Balance
  bool awb_enabled = true;
  float awb_speed = 0.1f;  // Vitesse d'adaptation (0.0-1.0)
  
  // Auto Exposure
  bool ae_enabled = true;
  uint32_t ae_target_luminance = 128;  // Cible 0-255
  uint32_t ae_tolerance = 20;
  float ae_speed = 0.15f;
  
  // Auto Focus (si supporté)
  bool af_enabled = false;
  
  // Histogram
  bool hist_enabled = true;
  
  // Sharpen
  bool sharpen_enabled = true;
  uint8_t sharpen_strength = 3;  // 0-10
  
  // Denoise (Bayer Filter)
  bool denoise_enabled = true;
  uint8_t denoise_level = 2;  // 0-10
  
  // Demosaic
  bool demosaic_enabled = true;
  float demosaic_gradient_ratio = 0.5f;
  
  // Color Correction Matrix
  bool ccm_enabled = true;
  
  // Gamma
  bool gamma_enabled = true;
  float gamma_value = 2.2f;
  
  // Color adjustments
  uint32_t brightness = 50;  // 0-100
  uint32_t contrast = 50;    // 0-100
  uint32_t saturation = 50;  // 0-100
  int32_t hue = 0;           // -180 to +180
};

// Statistiques historiques pour les algorithmes adaptatifs
struct IPAHistory {
  // AWB history
  float prev_red_gain = 1.0f;
  float prev_blue_gain = 1.0f;
  uint32_t prev_color_temp = 5000;
  
  // AE history
  uint32_t prev_exposure = 10000;
  float prev_gain = 1.0f;
  uint32_t prev_avg_luminance = 128;
  
  // Counters
  uint32_t frame_count = 0;
  uint32_t ae_stable_frames = 0;
  uint32_t awb_stable_frames = 0;
};

class CompleteIPA {
public:
  CompleteIPA() : config_(), history_() {}
  
  // Configuration
  void set_config(const IPAConfig &config) { config_ = config; }
  IPAConfig& get_config() { return config_; }
  
  // Initialisation
  esp_err_t init(const esp_ipa_sensor_t *sensor, esp_ipa_metadata_t *metadata);
  
  // Traitement principal
  void process(const esp_ipa_stats_t *stats, 
               const esp_ipa_sensor_t *sensor,
               esp_ipa_metadata_t *metadata);
  
  // Getters pour debug
  const IPAHistory& get_history() const { return history_; }
  
private:
  IPAConfig config_;
  IPAHistory history_;
  
  // Sensor info cache
  esp_ipa_sensor_t sensor_info_;
  
  // Algorithmes individuels
  void process_awb(const esp_ipa_stats_awb_t *awb, esp_ipa_metadata_t *metadata);
  void process_ae(const esp_ipa_stats_ae_t *ae, const esp_ipa_sensor_t *sensor, esp_ipa_metadata_t *metadata);
  void process_histogram(const esp_ipa_stats_hist_t *hist, esp_ipa_metadata_t *metadata);
  void process_sharpen(const esp_ipa_stats_sharpen_t *sharpen, esp_ipa_metadata_t *metadata);
  
  // Configuration des modules ISP
  void configure_denoise(esp_ipa_metadata_t *metadata);
  void configure_demosaic(esp_ipa_metadata_t *metadata);
  void configure_ccm(esp_ipa_metadata_t *metadata, uint32_t color_temp);
  void configure_gamma(esp_ipa_metadata_t *metadata);
  void configure_color_adjustments(esp_ipa_metadata_t *metadata);
  
  // Helpers
  uint32_t estimate_color_temperature(float red_gain, float blue_gain);
  void generate_ccm_for_temperature(uint32_t temp_k, float matrix[3][3]);
  void generate_gamma_curve(float gamma, uint8_t x_points[], uint8_t y_points[], size_t num_points);
  float smooth_value(float current, float target, float speed);
  void generate_sharpen_matrix(uint8_t strength, uint8_t matrix[3][3]);
  void generate_denoise_matrix(uint8_t level, uint8_t matrix[5][5]);
};

// ============================================================================
// IMPLÉMENTATION
// ============================================================================

inline esp_err_t CompleteIPA::init(const esp_ipa_sensor_t *sensor, esp_ipa_metadata_t *metadata) {
  if (!sensor || !metadata) {
    return ESP_ERR_INVALID_ARG;
  }
  
  // Sauvegarder les infos du capteur
  memcpy(&sensor_info_, sensor, sizeof(esp_ipa_sensor_t));
  
  // Réinitialiser l'historique
  history_.prev_exposure = sensor->cur_exposure;
  history_.prev_gain = sensor->cur_gain;
  history_.frame_count = 0;
  
  // Initialiser les métadonnées avec valeurs par défaut
  memset(metadata, 0, sizeof(esp_ipa_metadata_t));
  
  // Auto White Balance
  if (config_.awb_enabled) {
    metadata->flags |= IPA_METADATA_FLAGS_CT | IPA_METADATA_FLAGS_RG | IPA_METADATA_FLAGS_BG;
    metadata->color_temp = 5000;  // Lumière du jour
    metadata->red_gain = 1.0f;
    metadata->blue_gain = 1.0f;
  }
  
  // Auto Exposure
  if (config_.ae_enabled) {
    metadata->flags |= IPA_METADATA_FLAGS_ET | IPA_METADATA_FLAGS_GN;
    metadata->exposure = sensor->cur_exposure;
    metadata->gain = sensor->cur_gain;
  }
  
  // Denoise (Bayer Filter)
  if (config_.denoise_enabled) {
    configure_denoise(metadata);
  }
  
  // Demosaic
  if (config_.demosaic_enabled) {
    configure_demosaic(metadata);
  }
  
  // Sharpen
  if (config_.sharpen_enabled) {
    configure_sharpen(nullptr, metadata);
  }
  
  // Gamma
  if (config_.gamma_enabled) {
    configure_gamma(metadata);
  }
  
  // CCM
  if (config_.ccm_enabled) {
    configure_ccm(metadata, metadata->color_temp);
  }
  
  // Color adjustments
  configure_color_adjustments(metadata);
  
  return ESP_OK;
}

inline void CompleteIPA::process(const esp_ipa_stats_t *stats, 
                                 const esp_ipa_sensor_t *sensor,
                                 esp_ipa_metadata_t *metadata) {
  if (!stats || !sensor || !metadata) {
    return;
  }
  
  history_.frame_count++;
  
  // Réinitialiser les flags
  metadata->flags = 0;
  
  // Auto White Balance
  if (config_.awb_enabled && (stats->flags & IPA_STATS_FLAGS_AWB)) {
    process_awb(stats->awb_stats, metadata);
  }
  
  // Auto Exposure
  if (config_.ae_enabled && (stats->flags & IPA_STATS_FLAGS_AE)) {
    process_ae(stats->ae_stats, sensor, metadata);
  }
  
  // Histogram analysis
  if (config_.hist_enabled && (stats->flags & IPA_STATS_FLAGS_HIST)) {
    process_histogram(stats->hist_stats, metadata);
  }
  
  // Sharpen
  if (config_.sharpen_enabled && (stats->flags & IPA_STATS_FLAGS_SHARPEN)) {
    process_sharpen(&stats->sharpen_stats, metadata);
  }
  
  // Modules statiques (toujours actifs)
  if (config_.denoise_enabled) {
    configure_denoise(metadata);
  }
  
  if (config_.demosaic_enabled) {
    configure_demosaic(metadata);
  }
  
  if (config_.gamma_enabled) {
    configure_gamma(metadata);
  }
  
  if (config_.ccm_enabled) {
    configure_ccm(metadata, metadata->color_temp);
  }
  
  configure_color_adjustments(metadata);
}

inline void CompleteIPA::process_awb(const esp_ipa_stats_awb_t *awb, esp_ipa_metadata_t *metadata) {
  if (!awb || awb->counted == 0) {
    // Pas de pixels blancs détectés, garder les valeurs précédentes
    metadata->red_gain = history_.prev_red_gain;
    metadata->blue_gain = history_.prev_blue_gain;
    metadata->color_temp = history_.prev_color_temp;
    metadata->flags |= IPA_METADATA_FLAGS_CT | IPA_METADATA_FLAGS_RG | IPA_METADATA_FLAGS_BG;
    return;
  }
  
  // Calculer les moyennes RGB des patches blancs
  float avg_r = (float)awb->sum_r / awb->counted;
  float avg_g = (float)awb->sum_g / awb->counted;
  float avg_b = (float)awb->sum_b / awb->counted;
  
  // Éviter division par zéro
  if (avg_r < 1.0f) avg_r = 1.0f;
  if (avg_g < 1.0f) avg_g = 1.0f;
  if (avg_b < 1.0f) avg_b = 1.0f;
  
  // Calcul des gains (Gray World Algorithm amélioré)
  float target_red_gain = avg_g / avg_r;
  float target_blue_gain = avg_g / avg_b;
  
  // Limiter les gains à une plage raisonnable
  target_red_gain = std::max(0.3f, std::min(3.0f, target_red_gain));
  target_blue_gain = std::max(0.3f, std::min(3.0f, target_blue_gain));
  
  // Lissage temporel pour éviter les variations brusques
  float red_gain = smooth_value(history_.prev_red_gain, target_red_gain, config_.awb_speed);
  float blue_gain = smooth_value(history_.prev_blue_gain, target_blue_gain, config_.awb_speed);
  
  // Estimer la température de couleur
  uint32_t color_temp = estimate_color_temperature(red_gain, blue_gain);
  color_temp = (uint32_t)smooth_value(history_.prev_color_temp, color_temp, config_.awb_speed);
  
  // Mise à jour des métadonnées
  metadata->red_gain = red_gain;
  metadata->blue_gain = blue_gain;
  metadata->color_temp = color_temp;
  metadata->flags |= IPA_METADATA_FLAGS_CT | IPA_METADATA_FLAGS_RG | IPA_METADATA_FLAGS_BG;
  
  // Sauvegarder pour le prochain frame
  history_.prev_red_gain = red_gain;
  history_.prev_blue_gain = blue_gain;
  history_.prev_color_temp = color_temp;
  
  // Vérifier la stabilité
  float red_diff = fabs(red_gain - history_.prev_red_gain);
  float blue_diff = fabs(blue_gain - history_.prev_blue_gain);
  if (red_diff < 0.01f && blue_diff < 0.01f) {
    history_.awb_stable_frames++;
  } else {
    history_.awb_stable_frames = 0;
  }
}

inline void CompleteIPA::process_ae(const esp_ipa_stats_ae_t *ae, 
                                    const esp_ipa_sensor_t *sensor,
                                    esp_ipa_metadata_t *metadata) {
  if (!ae || !sensor) {
    return;
  }
  
  // Calculer la luminance moyenne de toutes les régions
  uint32_t total_luminance = 0;
  uint32_t valid_regions = 0;
  
  for (int i = 0; i < ISP_AE_REGIONS; i++) {
    if (ae[i].luminance > 0) {
      total_luminance += ae[i].luminance;
      valid_regions++;
    }
  }
  
  if (valid_regions == 0) {
    // Pas de données valides, garder les valeurs précédentes
    metadata->exposure = history_.prev_exposure;
    metadata->gain = history_.prev_gain;
    metadata->flags |= IPA_METADATA_FLAGS_ET | IPA_METADATA_FLAGS_GN;
    return;
  }
  
  uint32_t avg_luminance = total_luminance / valid_regions;
  
  // Calculer la différence avec la cible
  int32_t luminance_error = (int32_t)config_.ae_target_luminance - (int32_t)avg_luminance;
  
  // Si on est dans la tolérance, ne rien changer
  if (abs(luminance_error) <= (int32_t)config_.ae_tolerance) {
    metadata->exposure = history_.prev_exposure;
    metadata->gain = history_.prev_gain;
    metadata->flags |= IPA_METADATA_FLAGS_ET | IPA_METADATA_FLAGS_GN;
    history_.ae_stable_frames++;
    return;
  }
  
  history_.ae_stable_frames = 0;
  
  // Calculer le facteur de correction
  float correction_factor = 1.0f + (luminance_error / (float)config_.ae_target_luminance) * config_.ae_speed;
  correction_factor = std::max(0.5f, std::min(2.0f, correction_factor));
  
  uint32_t new_exposure = history_.prev_exposure;
  float new_gain = history_.prev_gain;
  
  if (luminance_error > 0) {
    // Image trop sombre, augmenter exposition d'abord, puis gain
    uint32_t target_exposure = (uint32_t)(history_.prev_exposure * correction_factor);
    
    if (target_exposure <= sensor->max_exposure) {
      new_exposure = target_exposure;
    } else {
      new_exposure = sensor->max_exposure;
      // Si on a atteint l'exposition max, augmenter le gain
      float gain_correction = correction_factor * (target_exposure / (float)sensor->max_exposure);
      new_gain = std::min(history_.prev_gain * gain_correction, sensor->max_gain);
    }
  } else {
    // Image trop claire, réduire gain d'abord, puis exposition
    float target_gain = history_.prev_gain * correction_factor;
    
    if (target_gain >= sensor->min_gain) {
      new_gain = target_gain;
    } else {
      new_gain = sensor->min_gain;
      // Si on a atteint le gain min, réduire l'exposition
      float exposure_correction = correction_factor * (target_gain / sensor->min_gain);
      new_exposure = std::max((uint32_t)(history_.prev_exposure * exposure_correction), sensor->min_exposure);
    }
  }
  
  // Mise à jour des métadonnées
  metadata->exposure = new_exposure;
  metadata->gain = new_gain;
  metadata->flags |= IPA_METADATA_FLAGS_ET | IPA_METADATA_FLAGS_GN;
  
  // Sauvegarder pour le prochain frame
  history_.prev_exposure = new_exposure;
  history_.prev_gain = new_gain;
  history_.prev_avg_luminance = avg_luminance;
}

inline void CompleteIPA::process_histogram(const esp_ipa_stats_hist_t *hist, esp_ipa_metadata_t *metadata) {
  if (!hist) return;
  
  // Analyse de l'histogramme pour ajustements avancés
  uint32_t total_pixels = 0;
  uint32_t dark_pixels = 0;
  uint32_t bright_pixels = 0;
  
  for (int i = 0; i < ISP_HIST_SEGMENT_NUMS; i++) {
    total_pixels += hist[i].value;
    if (i < ISP_HIST_SEGMENT_NUMS / 4) {
      dark_pixels += hist[i].value;
    } else if (i > (ISP_HIST_SEGMENT_NUMS * 3) / 4) {
      bright_pixels += hist[i].value;
    }
  }
  
  if (total_pixels == 0) return;
  
  float dark_ratio = (float)dark_pixels / total_pixels;
  float bright_ratio = (float)bright_pixels / total_pixels;
  
  // Ajustement automatique du contraste basé sur l'histogramme
  if (dark_ratio > 0.4f && bright_ratio < 0.1f) {
    // Image majoritairement sombre, augmenter luminosité
    config_.brightness = std::min(config_.brightness + 2, 100u);
  } else if (bright_ratio > 0.4f && dark_ratio < 0.1f) {
    // Image majoritairement claire, réduire luminosité
    config_.brightness = std::max(config_.brightness - 2, 0u);
  }
  
  // Ajuster le contraste si l'histogramme est trop concentré
  float spread = bright_ratio + dark_ratio;
  if (spread < 0.3f) {
    // Histogramme concentré au centre, augmenter contraste
    config_.contrast = std::min(config_.contrast + 1, 100u);
  }
}

inline void CompleteIPA::process_sharpen(const esp_ipa_stats_sharpen_t *sharpen, esp_ipa_metadata_t *metadata) {
  if (!sharpen) return;
  
  // Ajuster la force du sharpen selon le contenu haute fréquence
  uint8_t hf_value = sharpen->value;
  
  if (hf_value < 30) {
    // Peu de détails, augmenter sharpen
    config_.sharpen_strength = std::min(config_.sharpen_strength + 1, (uint8_t)10);
  } else if (hf_value > 200) {
    // Beaucoup de détails, réduire pour éviter artefacts
    config_.sharpen_strength = std::max(config_.sharpen_strength - 1, (uint8_t)0);
  }
  
  configure_sharpen(sharpen, metadata);
}

inline void CompleteIPA::configure_denoise(esp_ipa_metadata_t *metadata) {
  metadata->bf.level = config_.denoise_level;
  generate_denoise_matrix(config_.denoise_level, metadata->bf.matrix);
  metadata->flags |= IPA_METADATA_FLAGS_BF;
}

inline void CompleteIPA::configure_demosaic(esp_ipa_metadata_t *metadata) {
  metadata->demosaic.gradient_ratio = config_.demosaic_gradient_ratio;
  metadata->flags |= IPA_METADATA_FLAGS_DM;
}

inline void CompleteIPA::configure_sharpen(const esp_ipa_stats_sharpen_t *sharpen, esp_ipa_metadata_t *metadata) {
  // Thresholds basés sur la force
  uint8_t base_thresh = 20 + (config_.sharpen_strength * 5);
  metadata->sharpen.h_thresh = base_thresh + 20;
  metadata->sharpen.l_thresh = base_thresh;
  
  // Coefficients
  metadata->sharpen.h_coeff = 0.8f + (config_.sharpen_strength * 0.05f);
  metadata->sharpen.m_coeff = 0.5f + (config_.sharpen_strength * 0.03f);
  
  // Matrice de sharpen
  generate_sharpen_matrix(config_.sharpen_strength, metadata->sharpen.matrix);
  
  metadata->flags |= IPA_METADATA_FLAGS_SH;
}

inline void CompleteIPA::configure_ccm(esp_ipa_metadata_t *metadata, uint32_t color_temp) {
  generate_ccm_for_temperature(color_temp, metadata->ccm.matrix);
  metadata->flags |= IPA_METADATA_FLAGS_CCM;
}

inline void CompleteIPA::configure_gamma(esp_ipa_metadata_t *metadata) {
  generate_gamma_curve(config_.gamma_value, 
                      metadata->gamma.x, 
                      metadata->gamma.y, 
                      ISP_GAMMA_CURVE_POINTS_NUM);
  metadata->flags |= IPA_METADATA_FLAGS_GAMMA;
}

inline void CompleteIPA::configure_color_adjustments(esp_ipa_metadata_t *metadata) {
  metadata->brightness = config_.brightness;
  metadata->contrast = config_.contrast;
  metadata->saturation = config_.saturation;
  metadata->hue = config_.hue;
  metadata->flags |= IPA_METADATA_FLAGS_BR | IPA_METADATA_FLAGS_CN | 
                     IPA_METADATA_FLAGS_ST | IPA_METADATA_FLAGS_HUE;
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

inline uint32_t CompleteIPA::estimate_color_temperature(float red_gain, float blue_gain) {
  // Estimation approximative de la température de couleur
  // Basée sur les gains RGB
  
  // Plage typique: 2000K (tungstène) à 10000K (ciel bleu)
  float ratio = red_gain / blue_gain;
  
  // Courbe empirique
  uint32_t temp;
  if (ratio > 1.5f) {
    // Lumière chaude (tungstène, lever/coucher soleil)
    temp = 2000 + (uint32_t)((1.5f - ratio) * -1500.0f);
    temp = std::max(2000u, std::min(4000u, temp));
  } else if (ratio < 0.8f) {
    // Lumière froide (ombre, ciel nuageux)
    temp = 6000 + (uint32_t)((0.8f - ratio) * 5000.0f);
    temp = std::max(6000u, std::min(10000u, temp));
  } else {
    // Lumière du jour normale
    temp = 4000 + (uint32_t)((1.5f - ratio) * 2000.0f);
    temp = std::max(4000u, std::min(6500u, temp));
  }
  
  return temp;
}

inline void CompleteIPA::generate_ccm_for_temperature(uint32_t temp_k, float matrix[3][3]) {
  // Générer une matrice CCM adaptée à la température de couleur
  // Matrices de référence pour différentes températures
  
  if (temp_k < 3000) {
    // Tungstène (2800K-3000K)
    matrix[0][0] = 1.8f;  matrix[0][1] = -0.5f; matrix[0][2] = -0.3f;
    matrix[1][0] = -0.3f; matrix[1][1] = 1.5f;  matrix[1][2] = -0.2f;
    matrix[2][0] = -0.2f; matrix[2][1] = -0.7f; matrix[2][2] = 1.9f;
  } else if (temp_k < 4500) {
    // Fluorescent/tungstène chaud (3000K-4500K)
    matrix[0][0] = 1.5f;  matrix[0][1] = -0.3f; matrix[0][2] = -0.2f;
    matrix[1][0] = -0.2f; matrix[1][1] = 1.3f;  matrix[1][2] = -0.1f;
    matrix[2][0] = -0.1f; matrix[2][1] = -0.4f; matrix[2][2] = 1.5f;
  } else if (temp_k < 5500) {
    // Lumière du jour (4500K-5500K) - Matrice identité légèrement ajustée
    matrix[0][0] = 1.2f;  matrix[0][1] = -0.1f; matrix[0][2] = -0.1f;
    matrix[1][0] = -0.1f; matrix[1][1] = 1.2f;  matrix[1][2] = -0.1f;
    matrix[2][0] = -0.1f; matrix[2][1] = -0.2f; matrix[2][2] = 1.3f;
  } else if (temp_k < 7000) {
    // Lumière du jour directe (5500K-7000K)
    matrix[0][0] = 1.1f;  matrix[0][1] = 0.0f;  matrix[0][2] = -0.1f;
    matrix[1][0] = 0.0f;  matrix[1][1] = 1.1f;  matrix[1][2] = 0.0f;
    matrix[2][0] = -0.1f; matrix[2][1] = -0.1f; matrix[2][2] = 1.2f;
  } else {
    // Ombre/ciel nuageux (>7000K)
    matrix[0][0] = 1.0f;  matrix[0][1] = 0.1f;  matrix[0][2] = -0.1f;
    matrix[1][0] = 0.1f;  matrix[1][1] = 1.0f;  matrix[1][2] = 0.1f;
    matrix[2][0] = -0.1f; matrix[2][1] = 0.0f;  matrix[2][2] = 1.1f;
  }
}

inline void CompleteIPA::generate_gamma_curve(float gamma, uint8_t x_points[], uint8_t y_points[], size_t num_points) {
  // Générer une courbe gamma
  for (size_t i = 0; i < num_points; i++) {
    float x_norm = (float)i / (num_points - 1);  // 0.0 à 1.0
    float y_norm = powf(x_norm, 1.0f / gamma);   // Appliquer gamma
    
    x_points[i] = (uint8_t)(x_norm * 255.0f);
    y_points[i] = (uint8_t)(y_norm * 255.0f);
  }
}

inline float CompleteIPA::smooth_value(float current, float target, float speed) {
  // Lissage exponentiel
  return current + (target - current) * speed;
}

inline void CompleteIPA::generate_sharpen_matrix(uint8_t strength, uint8_t matrix[3][3]) {
  // Générer une matrice de sharpen (laplacien adaptatif)
  // Matrice 3x3 standard: center - edges
  
  if (strength == 0) {
    // Pas de sharpen (identité)
    matrix[0][0] = 0; matrix[0][1] = 0; matrix[0][2] = 0;
    matrix[1][0] = 0; matrix[1][1] = 1; matrix[1][2] = 0;
    matrix[2][0] = 0; matrix[2][1] = 0; matrix[2][2] = 0;
  } else {
    // Sharpen proportionnel à la force
    int8_t edge = -(int8_t)strength;
    int8_t center = 1 + (strength * 4);
    
    matrix[0][0] = 0;    matrix[0][1] = edge; matrix[0][2] = 0;
    matrix[1][0] = edge; matrix[1][1] = center; matrix[1][2] = edge;
    matrix[2][0] = 0;    matrix[2][1] = edge; matrix[2][2] = 0;
  }
}

inline void CompleteIPA::generate_denoise_matrix(uint8_t level, uint8_t matrix[5][5]) {
  // Générer une matrice de débruitage (filtre gaussien 5x5)
  
  if (level == 0) {
    // Pas de débruitage
    memset(matrix, 0, 25);
    matrix[2][2] = 1;  // Centre uniquement
  } else if (level <= 3) {
    // Débruitage léger (gaussian blur faible)
    const uint8_t light_blur[5][5] = {
      {1, 2, 3, 2, 1},
      {2, 4, 6, 4, 2},
      {3, 6, 9, 6, 3},
      {2, 4, 6, 4, 2},
      {1, 2, 3, 2, 1}
    };
    memcpy(matrix, light_blur, 25);
  } else if (level <= 6) {
    // Débruitage moyen
    const uint8_t medium_blur[5][5] = {
      {1, 3, 4, 3, 1},
      {3, 6, 8, 6, 3},
      {4, 8, 12, 8, 4},
      {3, 6, 8, 6, 3},
      {1, 3, 4, 3, 1}
    };
    memcpy(matrix, medium_blur, 25);
  } else {
    // Débruitage fort
    const uint8_t strong_blur[5][5] = {
      {2, 4, 5, 4, 2},
      {4, 8, 10, 8, 4},
      {5, 10, 15, 10, 5},
      {4, 8, 10, 8, 4},
      {2, 4, 5, 4, 2}
    };
    memcpy(matrix, strong_blur, 25);
  }
}

}  // namespace mipi_dsi_cam
}  // namespace esphome

#endif  // USE_ESP32_VARIANT_ESP32P4
