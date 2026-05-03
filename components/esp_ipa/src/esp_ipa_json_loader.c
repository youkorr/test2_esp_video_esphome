/*
 * SPDX-FileCopyrightText: 2025 Custom IPA JSON Loader
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "esp_log.h"
#include "esp_err.h"
#include "cJSON.h"
#include "esp_ipa.h"
#include "esp_ipa_json_loader.h"
#include "esp_video_isp_ioctl.h"
#include "linux/videodev2.h"

static const char *TAG = "ipa_json";

// Déclarations externes des JSON embarqués (créés par esp_video_build.py)
// Le script Python génère ces symboles via wrapper C
extern const char ov02c10_ipa_config_json_start[];
extern const char *ov02c10_ipa_config_json_end;
extern const size_t ov02c10_ipa_config_json_size;
extern const char ov5647_ipa_config_json_start[];
extern const char *ov5647_ipa_config_json_end;
extern const size_t ov5647_ipa_config_json_size;
extern const char sc2336_ipa_config_json_start[];
extern const char *sc2336_ipa_config_json_end;
extern const size_t sc2336_ipa_config_json_size;
extern const char sc202cs_ipa_config_json_start[];
extern const char *sc202cs_ipa_config_json_end;
extern const size_t sc202cs_ipa_config_json_size;

/* ----------------------------------------------------------------------
 * Build-time IPA stage toggles
 *
 * Some sensor JSONs (notably the SC202CS default tuning shipped by
 * Espressif's esp_cam_sensor) target a slightly different optical stack
 * than what M5Stack ships in the Tab5: the CCM in particular ends up
 * tinting the whole frame green and dropping reds. To let users iterate
 * without having to recompile the full firmware, each stage can be
 * disabled at build time:
 *
 *   ESP_IPA_DISABLE_CCM      skip CCM application
 *   ESP_IPA_DISABLE_AWB      skip AWB range application
 *   ESP_IPA_DISABLE_SHARPEN  skip sharpen application
 *   ESP_IPA_DISABLE_CONTRAST skip contrast application
 *
 * From ESPHome these are added via:
 *   esp32:
 *     framework:
 *       sdkconfig_options:
 *         ESP_IPA_DISABLE_CCM: "y"
 *
 * Or as a build flag:
 *   CFLAGS=-DESP_IPA_DISABLE_CCM=1
 * ---------------------------------------------------------------------- */
#ifndef ESP_IPA_DISABLE_CCM
#define ESP_IPA_DISABLE_CCM      0
#endif
#ifndef ESP_IPA_DISABLE_AWB
#define ESP_IPA_DISABLE_AWB      0
#endif
#ifndef ESP_IPA_DISABLE_SHARPEN
#define ESP_IPA_DISABLE_SHARPEN  0
#endif
#ifndef ESP_IPA_DISABLE_CONTRAST
#define ESP_IPA_DISABLE_CONTRAST 0
#endif

/**
 * @brief Parse CCM (Color Correction Matrix) from JSON
 *
 * Extrait la matrice CCM calibrée du JSON pour améliorer les couleurs
 */
static esp_err_t parse_ccm_from_json(cJSON *sensor_root, esp_ipa_ccm_config_t *ccm_config)
{
    cJSON *acc = cJSON_GetObjectItem(sensor_root, "acc");
    if (!acc) {
        ESP_LOGW(TAG, "No 'acc' section in JSON");
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *ccm = cJSON_GetObjectItem(acc, "ccm");
    if (!ccm) {
        ESP_LOGW(TAG, "No 'ccm' in 'acc' section");
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *table = cJSON_GetObjectItem(ccm, "table");
    if (!table || !cJSON_IsArray(table) || cJSON_GetArraySize(table) == 0) {
        ESP_LOGW(TAG, "No valid CCM table");
        return ESP_ERR_INVALID_ARG;
    }

    // Prendre le premier élément du tableau CCM
    cJSON *ccm_entry = cJSON_GetArrayItem(table, 0);
    if (!ccm_entry) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *color_temp = cJSON_GetObjectItem(ccm_entry, "color_temp");
    cJSON *matrix = cJSON_GetObjectItem(ccm_entry, "matrix");

    if (!matrix || !cJSON_IsArray(matrix)) {
        ESP_LOGE(TAG, "Invalid CCM matrix");
        return ESP_ERR_INVALID_ARG;
    }

    if (cJSON_GetArraySize(matrix) != 9) {
        ESP_LOGE(TAG, "CCM matrix must have 9 elements, got %d", cJSON_GetArraySize(matrix));
        return ESP_ERR_INVALID_SIZE;
    }

    // Extraire la matrice 3x3
    ccm_config->color_temp = color_temp ? color_temp->valueint : 0;
    for (int i = 0; i < 9; i++) {
        cJSON *value = cJSON_GetArrayItem(matrix, i);
        if (!value || !cJSON_IsNumber(value)) {
            ESP_LOGE(TAG, "Invalid CCM matrix value at index %d", i);
            return ESP_ERR_INVALID_ARG;
        }
        ccm_config->matrix[i / 3][i % 3] = (float)value->valuedouble;
    }

    ESP_LOGI(TAG, "CCM Matrix loaded:");
    ESP_LOGI(TAG, "  [%.3f, %.3f, %.3f]", ccm_config->matrix[0][0], ccm_config->matrix[0][1], ccm_config->matrix[0][2]);
    ESP_LOGI(TAG, "  [%.3f, %.3f, %.3f]", ccm_config->matrix[1][0], ccm_config->matrix[1][1], ccm_config->matrix[1][2]);
    ESP_LOGI(TAG, "  [%.3f, %.3f, %.3f]", ccm_config->matrix[2][0], ccm_config->matrix[2][1], ccm_config->matrix[2][2]);

    return ESP_OK;
}

/**
 * @brief Parse AWB (Auto White Balance) ranges from JSON
 */
static esp_err_t parse_awb_from_json(cJSON *sensor_root, esp_ipa_awb_config_t *awb_config)
{
    cJSON *awb = cJSON_GetObjectItem(sensor_root, "awb");
    if (!awb) {
        ESP_LOGW(TAG, "No 'awb' section in JSON");
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *range = cJSON_GetObjectItem(awb, "range");
    if (!range) {
        ESP_LOGW(TAG, "No 'range' in AWB section");
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *rg = cJSON_GetObjectItem(range, "rg");
    cJSON *bg = cJSON_GetObjectItem(range, "bg");

    if (!rg || !bg) {
        ESP_LOGE(TAG, "Missing RG or BG in AWB range");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *rg_min = cJSON_GetObjectItem(rg, "min");
    cJSON *rg_max = cJSON_GetObjectItem(rg, "max");
    cJSON *bg_min = cJSON_GetObjectItem(bg, "min");
    cJSON *bg_max = cJSON_GetObjectItem(bg, "max");

    if (!rg_min || !rg_max || !bg_min || !bg_max) {
        ESP_LOGE(TAG, "Incomplete AWB range values");
        return ESP_ERR_INVALID_ARG;
    }

    awb_config->rg_min = (float)rg_min->valuedouble;
    awb_config->rg_max = (float)rg_max->valuedouble;
    awb_config->bg_min = (float)bg_min->valuedouble;
    awb_config->bg_max = (float)bg_max->valuedouble;

    cJSON *min_counted = cJSON_GetObjectItem(awb, "min_counted");
    awb_config->min_counted = min_counted ? min_counted->valueint : 2000;

    ESP_LOGI(TAG, "AWB Ranges loaded:");
    ESP_LOGI(TAG, "  RG: %.3f - %.3f", awb_config->rg_min, awb_config->rg_max);
    ESP_LOGI(TAG, "  BG: %.3f - %.3f", awb_config->bg_min, awb_config->bg_max);
    ESP_LOGI(TAG, "  Min counted: %d", awb_config->min_counted);

    return ESP_OK;
}

/**
 * @brief Parse Sharpen parameters from JSON
 */
static esp_err_t parse_sharpen_from_json(cJSON *sensor_root, esp_ipa_sharpen_config_t *sharpen_config)
{
    cJSON *aen = cJSON_GetObjectItem(sensor_root, "aen");
    if (!aen) {
        ESP_LOGW(TAG, "No 'aen' section in JSON");
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *sharpen = cJSON_GetObjectItem(aen, "sharpen");
    if (!sharpen || !cJSON_IsArray(sharpen) || cJSON_GetArraySize(sharpen) == 0) {
        ESP_LOGW(TAG, "No valid sharpen array");
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *sharpen_entry = cJSON_GetArrayItem(sharpen, 0);
    if (!sharpen_entry) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *param = cJSON_GetObjectItem(sharpen_entry, "param");
    if (!param) {
        ESP_LOGE(TAG, "No 'param' in sharpen entry");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *h_thresh = cJSON_GetObjectItem(param, "h_thresh");
    cJSON *l_thresh = cJSON_GetObjectItem(param, "l_thresh");
    cJSON *h_coeff = cJSON_GetObjectItem(param, "h_coeff");
    cJSON *m_coeff = cJSON_GetObjectItem(param, "m_coeff");
    cJSON *matrix = cJSON_GetObjectItem(param, "matrix");

    if (h_thresh) sharpen_config->h_thresh = h_thresh->valueint;
    if (l_thresh) sharpen_config->l_thresh = l_thresh->valueint;
    if (h_coeff) sharpen_config->h_coeff = (float)h_coeff->valuedouble;
    if (m_coeff) sharpen_config->m_coeff = (float)m_coeff->valuedouble;

    if (matrix && cJSON_IsArray(matrix) && cJSON_GetArraySize(matrix) == 9) {
        for (int i = 0; i < 9; i++) {
            cJSON *value = cJSON_GetArrayItem(matrix, i);
            if (value && cJSON_IsNumber(value)) {
                sharpen_config->matrix[i / 3][i % 3] = (uint8_t)value->valueint;
            }
        }
        sharpen_config->has_matrix = true;
        ESP_LOGI(TAG, "  Matrix: [%d,%d,%d, %d,%d,%d, %d,%d,%d]",
                 sharpen_config->matrix[0][0], sharpen_config->matrix[0][1], sharpen_config->matrix[0][2],
                 sharpen_config->matrix[1][0], sharpen_config->matrix[1][1], sharpen_config->matrix[1][2],
                 sharpen_config->matrix[2][0], sharpen_config->matrix[2][1], sharpen_config->matrix[2][2]);
    } else {
        sharpen_config->has_matrix = false;
        ESP_LOGW(TAG, "  No valid sharpen matrix in JSON");
    }

    ESP_LOGI(TAG, "Sharpen params loaded:");
    ESP_LOGI(TAG, "  H thresh: %d, L thresh: %d", sharpen_config->h_thresh, sharpen_config->l_thresh);
    ESP_LOGI(TAG, "  H coeff: %.3f, M coeff: %.3f", sharpen_config->h_coeff, sharpen_config->m_coeff);

    return ESP_OK;
}

/**
 * @brief Parse Gamma parameters from JSON
 */
static esp_err_t parse_gamma_from_json(cJSON *sensor_root, esp_ipa_gamma_config_t *gamma_config)
{
    cJSON *aen = cJSON_GetObjectItem(sensor_root, "aen");
    if (!aen) {
        ESP_LOGW(TAG, "No 'aen' section in JSON");
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *gamma = cJSON_GetObjectItem(aen, "gamma");
    if (!gamma) {
        ESP_LOGW(TAG, "No 'gamma' in AEN section");
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *use_gamma_param = cJSON_GetObjectItem(gamma, "use_gamma_param");
    cJSON *table = cJSON_GetObjectItem(gamma, "table");

    if (use_gamma_param) {
        gamma_config->use_gamma_param = cJSON_IsTrue(use_gamma_param);
    }

    if (table && cJSON_IsArray(table) && cJSON_GetArraySize(table) > 0) {
        cJSON *gamma_entry = cJSON_GetArrayItem(table, 0);
        cJSON *gamma_param = cJSON_GetObjectItem(gamma_entry, "gamma_param");

        if (gamma_param) {
            gamma_config->gamma_param = (float)gamma_param->valuedouble;
            ESP_LOGI(TAG, "Gamma param loaded: %.3f", gamma_config->gamma_param);
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

/**
 * @brief Parse Contrast from JSON
 */
static esp_err_t parse_contrast_from_json(cJSON *sensor_root, esp_ipa_contrast_config_t *contrast_config)
{
    cJSON *aen = cJSON_GetObjectItem(sensor_root, "aen");
    if (!aen) {
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *contrast = cJSON_GetObjectItem(aen, "contrast");
    if (!contrast || !cJSON_IsArray(contrast) || cJSON_GetArraySize(contrast) == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *contrast_entry = cJSON_GetArrayItem(contrast, 0);
    if (!contrast_entry) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *value = cJSON_GetObjectItem(contrast_entry, "value");
    if (value) {
        contrast_config->value = value->valueint;
        ESP_LOGI(TAG, "Contrast loaded: %d", contrast_config->value);
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

/**
 * @brief Load IPA configuration from embedded JSON
 */
esp_err_t esp_ipa_load_json_config(const char *sensor_name, esp_ipa_json_config_t *ipa_json_config)
{
    if (!sensor_name || !ipa_json_config) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Loading IPA JSON config for sensor: %s", sensor_name);

    const char *json_data = NULL;
    size_t json_size = 0;

    if (strcasecmp(sensor_name, "OV02C10") == 0) {
        json_data = ov02c10_ipa_config_json_start;
        json_size = ov02c10_ipa_config_json_size;
        ESP_LOGI(TAG, "Using OV02C10 JSON (%zu bytes)", json_size);
    } else if (strcasecmp(sensor_name, "OV5647") == 0) {
        json_data = ov5647_ipa_config_json_start;
        json_size = ov5647_ipa_config_json_size;
        ESP_LOGI(TAG, "Using OV5647 JSON (%zu bytes)", json_size);
    } else if (strcasecmp(sensor_name, "SC2336") == 0) {
        json_data = sc2336_ipa_config_json_start;
        json_size = sc2336_ipa_config_json_size;
        ESP_LOGI(TAG, "Using SC2336 JSON (%zu bytes) [ESP32-P4 eco4]", json_size);
    } else if (strcasecmp(sensor_name, "SC202CS") == 0) {
        json_data = sc202cs_ipa_config_json_start;
        json_size = sc202cs_ipa_config_json_size;
        ESP_LOGI(TAG, "Using SC202CS JSON (%zu bytes) [M5Stack Tab5]", json_size);
    } else {
        ESP_LOGE(TAG, "Unknown sensor: %s", sensor_name);
        ESP_LOGE(TAG, "Supported sensors: OV02C10, OV5647, SC2336, SC202CS");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (!json_data || json_size == 0) {
        ESP_LOGE(TAG, "JSON data not available");
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *root = cJSON_Parse(json_data);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *sensor_root = cJSON_GetObjectItem(root, sensor_name);
    if (!sensor_root) {
        char sensor_lower[32];
        strncpy(sensor_lower, sensor_name, sizeof(sensor_lower) - 1);
        sensor_lower[sizeof(sensor_lower) - 1] = '\0';
        for (int i = 0; sensor_lower[i]; i++) {
            sensor_lower[i] = tolower((unsigned char)sensor_lower[i]);
        }
        sensor_root = cJSON_GetObjectItem(root, sensor_lower);
    }
    if (!sensor_root) {
        char sensor_upper[32];
        strncpy(sensor_upper, sensor_name, sizeof(sensor_upper) - 1);
        sensor_upper[sizeof(sensor_upper) - 1] = '\0';
        for (int i = 0; sensor_upper[i]; i++) {
            sensor_upper[i] = toupper((unsigned char)sensor_upper[i]);
        }
        sensor_root = cJSON_GetObjectItem(root, sensor_upper);
    }

    if (!sensor_root) {
        ESP_LOGE(TAG, "Sensor section '%s' not found in JSON", sensor_name);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Found sensor section in JSON");

    esp_err_t ret = ESP_OK;
    memset(ipa_json_config, 0, sizeof(esp_ipa_json_config_t));

    if (parse_ccm_from_json(sensor_root, &ipa_json_config->ccm) == ESP_OK) {
        ipa_json_config->has_ccm = true;
    }
    if (parse_awb_from_json(sensor_root, &ipa_json_config->awb) == ESP_OK) {
        ipa_json_config->has_awb = true;
    }
    if (parse_sharpen_from_json(sensor_root, &ipa_json_config->sharpen) == ESP_OK) {
        ipa_json_config->has_sharpen = true;
    }
    if (parse_gamma_from_json(sensor_root, &ipa_json_config->gamma) == ESP_OK) {
        ipa_json_config->has_gamma = true;
    }
    if (parse_contrast_from_json(sensor_root, &ipa_json_config->contrast) == ESP_OK) {
        ipa_json_config->has_contrast = true;
    }

    cJSON_Delete(root);

    ESP_LOGI(TAG, "IPA JSON loaded - CCM: %s, AWB: %s, Sharpen: %s, Gamma: %s",
             ipa_json_config->has_ccm ? "Yes" : "No",
             ipa_json_config->has_awb ? "Yes" : "No",
             ipa_json_config->has_sharpen ? "Yes" : "No",
             ipa_json_config->has_gamma ? "Yes" : "No");
    ESP_LOGI(TAG, "  Contrast: %s", ipa_json_config->has_contrast ? "Loaded" : "Not found");

    return ESP_OK;
}

/**
 * @brief Apply parsed JSON IPA configuration to ISP via V4L2
 */
esp_err_t esp_ipa_apply_json_to_isp(int isp_fd, const esp_ipa_json_config_t *ipa_json_config)
{
    if (isp_fd < 0 || !ipa_json_config) {
        ESP_LOGE(TAG, "Invalid ISP fd or config");
        return ESP_ERR_INVALID_ARG;
    }

    struct v4l2_ext_controls controls;
    struct v4l2_ext_control control[1];
    esp_err_t ret = ESP_OK;
    int applied_count = 0;

    ESP_LOGI(TAG, "Applying JSON IPA parameters to ISP...");

    // 1. CCM (Color Correction Matrix)
    //
    // The default JSON tunings are calibrated for Espressif's reference
    // boards. On boards with a different optical stack (typically the
    // M5Stack Tab5 with the SC202CS) the resulting CCM tints the image
    // green and crushes reds. Set ESP_IPA_DISABLE_CCM=1 to skip it.
    if (ipa_json_config->has_ccm) {
#if ESP_IPA_DISABLE_CCM
        ESP_LOGW(TAG, "  CCM SKIPPED (ESP_IPA_DISABLE_CCM=1)");
#else
        esp_video_isp_ccm_t ccm = { .enable = true };
        memcpy(ccm.matrix, ipa_json_config->ccm.matrix, sizeof(ccm.matrix));

        controls.ctrl_class = V4L2_CID_USER_CLASS;
        controls.count = 1;
        controls.controls = control;
        control[0].id = V4L2_CID_USER_ESP_ISP_CCM;
        control[0].p_u8 = (uint8_t *)&ccm;

        if (ioctl(isp_fd, VIDIOC_S_EXT_CTRLS, &controls) == 0) {
            ESP_LOGI(TAG, "  CCM matrix applied successfully");
            applied_count++;
        } else {
            ESP_LOGE(TAG, "  Failed to apply CCM matrix: errno=%d", errno);
            ret = ESP_FAIL;
        }
#endif
    }

    // 2. AWB ranges
    if (ipa_json_config->has_awb) {
#if ESP_IPA_DISABLE_AWB
        ESP_LOGW(TAG, "  AWB SKIPPED (ESP_IPA_DISABLE_AWB=1)");
#else
        esp_video_isp_awb_t awb = {
            .enable = true,
            .rg_min = ipa_json_config->awb.rg_min,
            .rg_max = ipa_json_config->awb.rg_max,
            .bg_min = ipa_json_config->awb.bg_min,
            .bg_max = ipa_json_config->awb.bg_max,
            .green_min = 0,
            .green_max = 255,
        };

        controls.ctrl_class = V4L2_CID_USER_CLASS;
        controls.count = 1;
        controls.controls = control;
        control[0].id = V4L2_CID_USER_ESP_ISP_AWB;
        control[0].p_u8 = (uint8_t *)&awb;

        if (ioctl(isp_fd, VIDIOC_S_EXT_CTRLS, &controls) == 0) {
            ESP_LOGI(TAG, "  AWB ranges applied (RG: %.3f-%.3f, BG: %.3f-%.3f)",
                     awb.rg_min, awb.rg_max, awb.bg_min, awb.bg_max);
            applied_count++;
        } else {
            ESP_LOGE(TAG, "  Failed to apply AWB ranges: errno=%d", errno);
            ret = ESP_FAIL;
        }
#endif
    }

    // 3. Sharpen
    if (ipa_json_config->has_sharpen) {
#if ESP_IPA_DISABLE_SHARPEN
        ESP_LOGW(TAG, "  Sharpen SKIPPED (ESP_IPA_DISABLE_SHARPEN=1)");
#else
        esp_video_isp_sharpen_t sharpen = {
            .enable = true,
            .h_thresh = ipa_json_config->sharpen.h_thresh,
            .l_thresh = ipa_json_config->sharpen.l_thresh,
            .h_coeff = ipa_json_config->sharpen.h_coeff,
            .m_coeff = ipa_json_config->sharpen.m_coeff,
        };

        if (ipa_json_config->sharpen.has_matrix) {
            memcpy(sharpen.matrix, ipa_json_config->sharpen.matrix, sizeof(sharpen.matrix));
        } else {
            memset(sharpen.matrix, 1, sizeof(sharpen.matrix));
        }

        controls.ctrl_class = V4L2_CID_USER_CLASS;
        controls.count = 1;
        controls.controls = control;
        control[0].id = V4L2_CID_USER_ESP_ISP_SHARPEN;
        control[0].p_u8 = (uint8_t *)&sharpen;

        if (ioctl(isp_fd, VIDIOC_S_EXT_CTRLS, &controls) == 0) {
            ESP_LOGI(TAG, "  Sharpen applied (H:%d, L:%d, Hc:%.2f, Mc:%.2f)",
                     sharpen.h_thresh, sharpen.l_thresh, sharpen.h_coeff, sharpen.m_coeff);
            applied_count++;
        } else {
            ESP_LOGE(TAG, "  Failed to apply sharpen: errno=%d", errno);
        }
#endif
    }

    // 4. Gamma
    if (ipa_json_config->has_gamma && ipa_json_config->gamma.use_gamma_param) {
        ESP_LOGW(TAG, "  Gamma parameter found but V4L2 gamma control requires point coordinates");
        ESP_LOGW(TAG, "     Using brightness/contrast controls instead");
    }

    // 5. Contrast
    if (ipa_json_config->has_contrast) {
#if ESP_IPA_DISABLE_CONTRAST
        ESP_LOGW(TAG, "  Contrast SKIPPED (ESP_IPA_DISABLE_CONTRAST=1)");
#else
        controls.ctrl_class = V4L2_CID_USER_CLASS;
        controls.count = 1;
        controls.controls = control;
        control[0].id = V4L2_CID_CONTRAST;
        control[0].value = ipa_json_config->contrast.value;

        if (ioctl(isp_fd, VIDIOC_S_EXT_CTRLS, &controls) == 0) {
            ESP_LOGI(TAG, "  Contrast applied: %d", ipa_json_config->contrast.value);
            applied_count++;
        } else {
            ESP_LOGE(TAG, "  Failed to apply contrast: errno=%d", errno);
        }
#endif
    }

    ESP_LOGI(TAG, "JSON IPA application complete: %d/%d parameters applied successfully",
             applied_count,
             (ipa_json_config->has_ccm ? 1 : 0) +
             (ipa_json_config->has_awb ? 1 : 0) +
             (ipa_json_config->has_sharpen ? 1 : 0) +
             (ipa_json_config->has_contrast ? 1 : 0));

    return applied_count > 0 ? ESP_OK : ret;
}
