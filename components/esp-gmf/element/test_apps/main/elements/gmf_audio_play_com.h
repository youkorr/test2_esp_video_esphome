/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_gmf_pipeline.h"
#include "esp_gmf_io_codec_dev.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void play_pause_single_file( esp_gmf_pipeline_handle_t pipe, const char *uri);

void play_loop_single_file(esp_gmf_pipeline_handle_t pipe, esp_audio_type_t type);
void gmf_register_audio_all(esp_gmf_pool_handle_t pool);
void gmf_unregister_audio_all(esp_gmf_pool_handle_t pool);

void gmf_setup_pipeline_out_dev(esp_gmf_pipeline_handle_t pipe);
void gmf_setup_pipeline_in_dev(esp_gmf_pipeline_handle_t pipe);

#ifdef __cplusplus
}
#endif /* __cplusplus */
