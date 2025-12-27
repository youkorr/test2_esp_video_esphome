/*
 * SPDX-FileCopyrightText: 2025 Custom IPA JSON Loader
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief CCM (Color Correction Matrix) configuration from JSON
 *
 * Matrice 3x3 pour corriger les couleurs et éliminer les teintes
 */
typedef struct {
    uint32_t color_temp;         /*!< Color temperature in Kelvin */
    float matrix[3][3];          /*!< 3x3 Color correction matrix */
} esp_ipa_ccm_config_t;

/**
 * @brief AWB (Auto White Balance) configuration from JSON
 *
 * Plages RG/BG optimisées pour une meilleure balance des blancs
 */
typedef struct {
    float rg_min;                /*!< RG ratio minimum */
    float rg_max;                /*!< RG ratio maximum */
    float bg_min;                /*!< BG ratio minimum */
    float bg_max;                /*!< BG ratio maximum */
    uint32_t min_counted;        /*!< Minimum white patches to count */
} esp_ipa_awb_config_t;

/**
 * @brief Sharpen configuration from JSON
 */
typedef struct {
    uint8_t h_thresh;            /*!< High threshold */
    uint8_t l_thresh;            /*!< Low threshold */
    float h_coeff;               /*!< High coefficient */
    float m_coeff;               /*!< Medium coefficient */
    bool has_matrix;             /*!< Matrix is available */
    uint8_t matrix[3][3];        /*!< 3x3 sharpen matrix */
} esp_ipa_sharpen_config_t;

/**
 * @brief Gamma configuration from JSON
 */
typedef struct {
    bool use_gamma_param;        /*!< Use gamma parameter (vs curve) */
    float gamma_param;           /*!< Gamma parameter value */
} esp_ipa_gamma_config_t;

/**
 * @brief Contrast configuration from JSON
 */
typedef struct {
    uint32_t value;              /*!< Contrast value */
} esp_ipa_contrast_config_t;

/**
 * @brief Complete IPA JSON configuration
 *
 * Contient tous les paramètres parsés du JSON IPA
 */
typedef struct {
    bool has_ccm;                /*!< CCM configuration available */
    esp_ipa_ccm_config_t ccm;    /*!< CCM configuration */

    bool has_awb;                /*!< AWB configuration available */
    esp_ipa_awb_config_t awb;    /*!< AWB configuration */

    bool has_sharpen;            /*!< Sharpen configuration available */
    esp_ipa_sharpen_config_t sharpen; /*!< Sharpen configuration */

    bool has_gamma;              /*!< Gamma configuration available */
    esp_ipa_gamma_config_t gamma;     /*!< Gamma configuration */

    bool has_contrast;           /*!< Contrast configuration available */
    esp_ipa_contrast_config_t contrast; /*!< Contrast configuration */
} esp_ipa_json_config_t;

/**
 * @brief Load IPA configuration from embedded JSON
 *
 * Parse le fichier JSON embarqué et extrait tous les paramètres IPA
 * optimisés pour le capteur spécifié.
 *
 * @param sensor_name Sensor name (e.g., "OV02C10", "OV5647")
 * @param ipa_json_config Output structure with parsed JSON config
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if parameters are NULL
 *      - ESP_ERR_NOT_SUPPORTED if sensor is unknown
 *      - ESP_ERR_NOT_FOUND if JSON data not available
 */
esp_err_t esp_ipa_load_json_config(const char *sensor_name, esp_ipa_json_config_t *ipa_json_config);

/**
 * @brief Apply parsed JSON IPA configuration to ISP via V4L2
 *
 * Applique les paramètres calibrés du JSON au matériel ISP pour
 * corriger les couleurs délavées et améliorer la qualité d'image.
 *
 * Cette fonction configure :
 * - CCM (Color Correction Matrix) : Corrige les couleurs et teintes
 * - AWB ranges : Plages optimisées pour la balance des blancs
 * - Sharpen : Améliore la netteté de l'image
 * - Contrast : Ajuste le contraste
 *
 * @param isp_fd ISP device file descriptor (from open("/dev/video0"))
 * @param ipa_json_config Parsed JSON configuration
 * @return
 *      - ESP_OK if at least one parameter was applied successfully
 *      - ESP_ERR_INVALID_ARG if parameters are invalid
 *      - ESP_FAIL if all parameters failed to apply
 */
esp_err_t esp_ipa_apply_json_to_isp(int isp_fd, const esp_ipa_json_config_t *ipa_json_config);

#ifdef __cplusplus
}
#endif
