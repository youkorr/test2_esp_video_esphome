/**
 * Simplified GMF PPA implementation for YUV420RGB565 conversion
 * Based on esp-gmf/elements/gmf_video/esp_gmf_video_ppa.c
 *
 * Supports:
 * - PPA hardware acceleration (YUV420RGB565 with BT.601/BT.709)
 * - Software LUT fallback (if PPA unavailable)
 *
 * NOTE: 2D-DMA CSC only supports RGBRGB conversions (RGB888RGB565).
 *       For YUVRGB, PPA is the only hardware option.
 *
 * Author: Simplified from Espressif GMF code
 * License: LicenseRef-Espressif-Modified-MIT
 */

// Force ESP32-P4 target definition for PPA hardware
#ifndef CONFIG_IDF_TARGET_ESP32P4
  #define CONFIG_IDF_TARGET_ESP32P4 1
#endif

#include "gmf_ppa_simple.h"

#ifdef CONFIG_IDF_TARGET_ESP32P4

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "driver/ppa.h"
#include "esp_private/dma2d.h"
#include "hal/dma2d_types.h"
#include "hal/color_types.h"
#include "soc/dma2d_channel.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstring>

static const char *TAG = "gmf_ppa_simple";

// ============================================================================
// Internal structures (simplified from GMF)
// ============================================================================

typedef bool (*dma2d_m2m_trans_eof_callback_t)(void *user_data);

typedef struct {
    intptr_t                        tx_desc_base_addr;
    intptr_t                        rx_desc_base_addr;
    dma2d_m2m_trans_eof_callback_t  trans_eof_cb;
    void                           *user_data;
    dma2d_transfer_ability_t        transfer_ability;
    dma2d_csc_config_t             *tx_csc_config;
} dma2d_m2m_trans_config_t;

typedef struct {
    dma2d_m2m_trans_config_t  m2m_trans_desc;
    dma2d_trans_config_t      dma_chan_desc;
    uint8_t                   dma_trans_placeholder_head[64];
} dma2d_m2m_transaction_t;

typedef struct {
    dma2d_descriptor_t      *rx_desc;
    dma2d_descriptor_t      *tx_desc;
    dma2d_pool_handle_t      handle;
    SemaphoreHandle_t        sema;
    dma2d_m2m_transaction_t  trans;
    dma2d_csc_config_t       tx_cvt;
} dma2d_info_t;

// Global converter state
static struct {
    bool initialized;
    ppa_client_handle_t ppa_handle;
    ppa_srm_oper_config_t ppa_config;
    bool use_dma2d;  // true = use 2D-DMA, false = use PPA
    dma2d_info_t dma2d_info;
    uint16_t width;
    uint16_t height;
} g_gmf_ppa = {0};

// ============================================================================
// 2D-DMA callbacks and helpers
// ============================================================================

static bool IRAM_ATTR dma2d_m2m_suc_eof_event_cb(void *user_data) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    SemaphoreHandle_t sem = (SemaphoreHandle_t) user_data;
    xSemaphoreGiveFromISR(sem, &xHigherPriorityTaskWoken);
    return (xHigherPriorityTaskWoken == pdTRUE);
}

static bool dma2d_m2m_transaction_done_cb(dma2d_channel_handle_t dma2d_chan,
                                          dma2d_event_data_t *event_data,
                                          void *user_data) {
    bool need_yield = false;
    dma2d_m2m_transaction_t *trans_config = (dma2d_m2m_transaction_t*) user_data;
    dma2d_m2m_trans_config_t *m2m_trans_desc = &trans_config->m2m_trans_desc;
    if (m2m_trans_desc->trans_eof_cb) {
        need_yield |= m2m_trans_desc->trans_eof_cb(m2m_trans_desc->user_data);
    }
    return need_yield;
}

static bool dma2d_m2m_transaction_on_picked(uint32_t channel_num,
                                            const dma2d_trans_channel_info_t *dma2d_chans,
                                            void *user_config) {
    dma2d_m2m_transaction_t  *trans_config = (dma2d_m2m_transaction_t*) user_config;
    dma2d_m2m_trans_config_t *m2m_trans_desc = &trans_config->m2m_trans_desc;

    // Get the required 2D-DMA channel handles
    uint32_t dma_tx_chan_idx = 0;
    uint32_t dma_rx_chan_idx = 1;
    if (dma2d_chans[0].dir == DMA2D_CHANNEL_DIRECTION_RX) {
        dma_tx_chan_idx = 1;
        dma_rx_chan_idx = 0;
    }

    dma2d_channel_handle_t dma_tx_chan = dma2d_chans[dma_tx_chan_idx].chan;
    dma2d_channel_handle_t dma_rx_chan = dma2d_chans[dma_rx_chan_idx].chan;

    dma2d_trigger_t trig_periph = {
        .periph = DMA2D_TRIG_PERIPH_M2M,
        .periph_sel_id = SOC_DMA2D_TRIG_PERIPH_M2M_TX,
    };
    dma2d_connect(dma_tx_chan, &trig_periph);
    trig_periph.periph_sel_id = SOC_DMA2D_TRIG_PERIPH_M2M_RX;
    dma2d_connect(dma_rx_chan, &trig_periph);

    dma2d_set_transfer_ability(dma_tx_chan, &m2m_trans_desc->transfer_ability);
    dma2d_set_transfer_ability(dma_rx_chan, &m2m_trans_desc->transfer_ability);

    if (m2m_trans_desc->tx_csc_config) {
        dma2d_configure_color_space_conversion(dma_tx_chan, m2m_trans_desc->tx_csc_config);
    }

    dma2d_rx_event_callbacks_t dma_cbs = {
        .on_recv_eof = dma2d_m2m_transaction_done_cb,
    };
    dma2d_register_rx_event_callbacks(dma_rx_chan, &dma_cbs, (void *)trans_config);
    dma2d_set_desc_addr(dma_tx_chan, m2m_trans_desc->tx_desc_base_addr);
    dma2d_set_desc_addr(dma_rx_chan, m2m_trans_desc->rx_desc_base_addr);
    dma2d_start(dma_tx_chan);
    dma2d_start(dma_rx_chan);
    return false;
}

static void dma2d_link_dscr_init(dma2d_descriptor_t *dma2d, uint32_t *next, void *buf_ptr,
                                 uint32_t ha, uint32_t va, uint32_t hb, uint32_t vb,
                                 uint32_t eof, uint32_t en_2d, uint32_t pbyte,
                                 uint32_t mod, uint32_t bias_x, uint32_t bias_y) {
    dma2d->owner = DMA2D_DESCRIPTOR_BUFFER_OWNER_DMA;
    dma2d->suc_eof = eof;
    dma2d->dma2d_en = en_2d;
    dma2d->err_eof = 0;
    dma2d->hb_length = hb;
    dma2d->vb_size = vb;
    dma2d->pbyte = pbyte;
    dma2d->ha_length = ha;
    dma2d->va_size = va;
    dma2d->mode = mod;
    dma2d->y = bias_y;
    dma2d->x = bias_x;
    dma2d->buffer = buf_ptr;
    dma2d->next = (dma2d_descriptor_t*) next;
}

// ============================================================================
// 2D-DMA initialization for YUV420RGB565
// ============================================================================

static esp_err_t init_dma2d_yuv420_to_rgb565(uint16_t width, uint16_t height) {
    // NOTE: 2D-DMA CSC in ESP-IDF only supports RGBRGB conversions:
    //   - RGB888 RGB565 (DMA2D_CSC_TX_RGB888_TO_RGB565 / DMA2D_CSC_TX_RGB565_TO_RGB888)
    //
    // For YUV420RGB565 conversion, we MUST use PPA hardware instead.
    // The PPA supports:
    //   - YUV420/YUV422/YUV444 RGB565/RGB888
    //   - Color space standards (BT.601, BT.709)
    //   - Color range (limited/full)
    //
    // See components/esp-gmf/element/gmf_video/esp_gmf_video_ppa.c lines 409-418
    // for reference - GMF only uses 2D-DMA for RGBRGB conversions.

    ESP_LOGW(TAG, "2D-DMA does not support YUVRGB conversion");
    return ESP_ERR_NOT_SUPPORTED;
}

// ============================================================================
// PPA initialization for YUV420RGB565 (fallback)
// ============================================================================

static esp_err_t init_ppa_yuv420_to_rgb565(uint16_t width, uint16_t height) {
    ppa_client_config_t ppa_client_config = {
        .oper_type = PPA_OPERATION_SRM,
    };

    esp_err_t ret = ppa_register_client(&ppa_client_config, &g_gmf_ppa.ppa_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register PPA client: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure PPA for YUV420RGB565
    memset(&g_gmf_ppa.ppa_config, 0, sizeof(g_gmf_ppa.ppa_config));

    // Input (YUV420)
    g_gmf_ppa.ppa_config.in.pic_w = width;
    g_gmf_ppa.ppa_config.in.pic_h = height;
    g_gmf_ppa.ppa_config.in.block_w = width;
    g_gmf_ppa.ppa_config.in.block_h = height;
    g_gmf_ppa.ppa_config.in.block_offset_x = 0;
    g_gmf_ppa.ppa_config.in.block_offset_y = 0;
    g_gmf_ppa.ppa_config.in.srm_cm = PPA_SRM_COLOR_MODE_YUV420;
    g_gmf_ppa.ppa_config.in.yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601;
    g_gmf_ppa.ppa_config.in.yuv_range = PPA_COLOR_RANGE_LIMIT;

    // Output (RGB565)
    g_gmf_ppa.ppa_config.out.pic_w = width;
    g_gmf_ppa.ppa_config.out.pic_h = height;
    g_gmf_ppa.ppa_config.out.block_offset_x = 0;
    g_gmf_ppa.ppa_config.out.block_offset_y = 0;
    g_gmf_ppa.ppa_config.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

    // No transformation
    g_gmf_ppa.ppa_config.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
    g_gmf_ppa.ppa_config.scale_x = 1.0f;
    g_gmf_ppa.ppa_config.scale_y = 1.0f;
    g_gmf_ppa.ppa_config.mirror_x = false;
    g_gmf_ppa.ppa_config.mirror_y = false;
    g_gmf_ppa.ppa_config.rgb_swap = false;
    g_gmf_ppa.ppa_config.byte_swap = false;
    g_gmf_ppa.ppa_config.mode = PPA_TRANS_MODE_BLOCKING;

    ESP_LOGI(TAG, "PPA initialized for YUV420RGB565 (%dx%d)", width, height);
    return ESP_OK;
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t gmf_ppa_init() {
    // CRITICAL: Add printf() to ensure this log is NEVER filtered
    printf("\n========================================\n");
    printf(">>> GMF_PPA_INIT() CALLED <<<\n");
    printf(">>> CONFIG_IDF_TARGET_ESP32P4 = %d\n", CONFIG_IDF_TARGET_ESP32P4);
    printf("========================================\n\n");

    if (g_gmf_ppa.initialized) {
        ESP_LOGD(TAG, "GMF PPA already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing GMF PPA hardware...");

    // Use PPA hardware for YUV420RGB565 conversion
    // NOTE: 2D-DMA CSC only supports RGBRGB, not YUVRGB
    g_gmf_ppa.use_dma2d = false;
    ESP_LOGI(TAG, "GMF PPA ready (PPA hardware for YUVRGB, software fallback available)");

    g_gmf_ppa.initialized = true;

    printf("\n>>> GMF PPA INITIALIZED SUCCESSFULLY <<<\n\n");
    return ESP_OK;
}

esp_err_t gmf_ppa_convert_yuv420_to_rgb565(const uint8_t *yuv_in, uint8_t *rgb_out,
                                            uint16_t width, uint16_t height) {
    if (!g_gmf_ppa.initialized) {
        ESP_LOGE(TAG, "GMF PPA not initialized! Call gmf_ppa_init() first");
        return ESP_ERR_INVALID_STATE;
    }

    // Initialize PPA for this resolution if needed
    if (g_gmf_ppa.width != width || g_gmf_ppa.height != height) {
        // Cleanup old resources
        if (g_gmf_ppa.ppa_handle) {
            ppa_unregister_client(g_gmf_ppa.ppa_handle);
            g_gmf_ppa.ppa_handle = nullptr;
        }

        // Initialize PPA hardware
        esp_err_t ret = init_ppa_yuv420_to_rgb565(width, height);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize PPA: %s", esp_err_to_name(ret));
            return ret;
        }

        g_gmf_ppa.width = width;
        g_gmf_ppa.height = height;
    }

    // Perform PPA conversion (YUV420RGB565)
    g_gmf_ppa.ppa_config.in.buffer = (void*)yuv_in;
    g_gmf_ppa.ppa_config.out.buffer = (void*)rgb_out;
    g_gmf_ppa.ppa_config.out.buffer_size = width * height * 2;  // RGB565

    esp_err_t ret = ppa_do_scale_rotate_mirror(g_gmf_ppa.ppa_handle, &g_gmf_ppa.ppa_config);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "PPA conversion failed: %s (using software)", esp_err_to_name(ret));
    }
    return ret;
}

void gmf_ppa_deinit() {
    if (!g_gmf_ppa.initialized) {
        return;
    }

    if (g_gmf_ppa.ppa_handle) {
        ppa_unregister_client(g_gmf_ppa.ppa_handle);
        g_gmf_ppa.ppa_handle = nullptr;
    }

    memset(&g_gmf_ppa, 0, sizeof(g_gmf_ppa));
    ESP_LOGI(TAG, "GMF PPA deinitialized");
}

#endif // CONFIG_IDF_TARGET_ESP32P4
