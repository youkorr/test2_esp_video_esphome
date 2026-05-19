/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#include <string.h>
#include "esp_log.h"
#include "esp_ipa.h"

// Version par défaut si non définie
#ifndef ESP_IPA_VER_MAJOR
#define ESP_IPA_VER_MAJOR 1
#endif
#ifndef ESP_IPA_VER_MINOR
#define ESP_IPA_VER_MINOR 0
#endif
#ifndef ESP_IPA_VER_PATCH
#define ESP_IPA_VER_PATCH 0
#endif

static const char *TAG = "esp_ipa";

/**
 * @brief Print esp-ipa version string.
 *
 * @return None
 */

/**
 * @brief Print esp-ipa version string.
 *
 * @return None
 */
void esp_ipa_print_version(void)
{
    ESP_LOGI(TAG, "ESP-IPA version: %d.%d.%d", ESP_IPA_VER_MAJOR, ESP_IPA_VER_MINOR, ESP_IPA_VER_PATCH);
}

/**
 * @brief Get IPA pipeline configuration for specified camera sensor
 *
 * Configuration IPA conditionnelle par capteur :
 * - OV5647 : AWB+Denoise+Sharpen+Gamma (4 algos, CCM désactivée pour fix teinte rouge)
 * - SC202CS, OV02C10, autres : AWB+Denoise+Sharpen+Gamma+CCM (5 algos complets)
 *
 * Pipeline: Capteur (RAW8/RAW10) → ISP → IPA → RGB565
 *
 * Algorithmes disponibles (vérifiés dans libesp_ipa.a):
 * - awb.gray: Auto White Balance (balance des blancs automatique)
 * - denoising.gain_feedback: Réduction du bruit
 * - sharpen.freq_feedback: Netteté de l'image
 * - gamma.lumma_feedback: Correction gamma (luminosité)
 * - cc.linear: Matrice de correction couleur (CCM)
 *
 * Note: AEC/AGC n'est PAS disponible dans cette version de libesp_ipa.a
 * L'exposition doit être contrôlée manuellement via les méthodes V4L2:
 * - set_exposure(value) pour contrôle manuel
 * - set_gain(value) pour ajuster le gain
 *
 * @param cam_name Camera sensor name (e.g., "SC202CS", "OV5647", "OV02C10")
 * @return IPA configuration pointer for the camera, NULL if not found
 */
const esp_ipa_config_t *esp_ipa_pipeline_get_config(const char *cam_name)
{
    // Configuration pour OV5647 : CCM désactivée pour éviter teinte rouge.
    //
    // Note historique: une tentative d'ajouter "agc.threshold" pour stabiliser
    // l'exposition (comme le SC2336 chez Waveshare) a échoué car le driver
    // OV5647 ne supporte pas V4L2_CID_GAIN via VIDIOC_QUERY_EXT_CTRL.
    // L'algo AGC se charge mais crashe à la première mise à jour de gain avec
    // "ISP: failed to query gain". Donc on garde l'AEC built-in du capteur.
    static const char *ipa_names_ov5647[] = {
        "awb.gray",                /* Auto White Balance */
        "denoising.gain_feedback", /* Réduction bruit */
        "sharpen.freq_feedback",   /* Netteté */
        "gamma.lumma_feedback",    /* Correction gamma */
        // "cc.linear" DISABLED for OV5647: CCM causes red tint (amplifies red 2.0x)
        // "agc.threshold" DISABLED for OV5647: requires V4L2_CID_GAIN support which the driver lacks
    };

    static const esp_ipa_config_t ipa_config_ov5647 = {
        .ipa_nums = 4,     /* 4 IPAs: awb + denoising + sharpen + gamma (CCM and AGC disabled) */
        .ipa_names = ipa_names_ov5647,
    };

    // Configuration complète pour SC202CS, OV02C10, et autres : CCM activée
    static const char *ipa_names_full[] = {
        "awb.gray",                /* Auto White Balance */
        "denoising.gain_feedback", /* Réduction bruit */
        "sharpen.freq_feedback",   /* Netteté */
        "gamma.lumma_feedback",    /* Correction gamma */
        "cc.linear",               /* Color Correction Matrix */
    };

    static const esp_ipa_config_t ipa_config_full = {
        .ipa_nums = 5,     /* 5 IPAs complets */
        .ipa_names = ipa_names_full,
    };

    // Sélection conditionnelle par capteur
    if (cam_name) {
        if (strcmp(cam_name, "OV5647") == 0 || strcmp(cam_name, "ov5647") == 0) {
            ESP_LOGW(TAG, "IPA config for %s: AWB+Denoise+Sharpen+Gamma (4 algos, CCM disabled)", cam_name);
            return &ipa_config_ov5647;
        } else if (strcmp(cam_name, "SC202CS") == 0 || strcmp(cam_name, "sc202cs") == 0) {
            // SC202CS: Désactiver CCM pour éviter le fond vert (même config que OV5647)
            ESP_LOGW(TAG, "IPA config for %s: AWB+Denoise+Sharpen+Gamma (4 algos, CCM disabled - fixes green tint)", cam_name);
            return &ipa_config_ov5647;
        } else {
            ESP_LOGW(TAG, "IPA config for %s: AWB+Denoise+Sharpen+Gamma+CCM (5 algos, full pipeline)", cam_name);
            return &ipa_config_full;
        }
    }

    ESP_LOGW(TAG, "No camera name provided - using full IPA config with CCM");
    return &ipa_config_full;
}
