/**
 * Simplified GMF PPA implementation for YUV420→RGB565 conversion
 * Based on esp-gmf/elements/gmf_video/esp_gmf_video_ppa.c
 *
 * Supports:
 * - 2D-DMA color space conversion (fastest, if supported)
 * - PPA hardware acceleration (fallback)
 *
 * Author: Simplified from Espressif GMF code
 * License: LicenseRef-Espressif-Modified-MIT
 */

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
// 2D-DMA initialization for YUV420→RGB565
// ============================================================================

static esp_err_t init_dma2d_yuv420_to_rgb565(uint16_t width, uint16_t height) {
    dma2d_info_t *dma2d = &g_gmf_ppa.dma2d_info;

    dma2d_pool_config_t pool_config = {
        .pool_id = 0,
    };

    esp_err_t ret = dma2d_acquire_pool(&pool_config, &dma2d->handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate 2D-DMA pool: %s", esp_err_to_name(ret));
        return ret;
    }

    // Allocate descriptors (cache-aligned for PSRAM)
    uint8_t align = 64;  // ESP32-P4 cache line size
    dma2d->tx_desc = (dma2d_descriptor_t *)heap_caps_aligned_calloc(align, 1, 64, MALLOC_CAP_SPIRAM);
    dma2d->rx_desc = (dma2d_descriptor_t *)heap_caps_aligned_calloc(align, 1, 64, MALLOC_CAP_SPIRAM);
    dma2d->sema = xSemaphoreCreateCounting(1, 0);

    if (!dma2d->sema || !dma2d->rx_desc || !dma2d->tx_desc) {
        ESP_LOGE(TAG, "Failed to allocate 2D-DMA descriptors/semaphore");
        if (dma2d->tx_desc) heap_caps_free(dma2d->tx_desc);
        if (dma2d->rx_desc) heap_caps_free(dma2d->rx_desc);
        if (dma2d->sema) vSemaphoreDelete(dma2d->sema);
        return ESP_ERR_NO_MEM;
    }

    // Configure DMA channel
    dma2d->trans.dma_chan_desc.tx_channel_num = 1;
    dma2d->trans.dma_chan_desc.rx_channel_num = 1;
    dma2d->trans.dma_chan_desc.channel_flags = DMA2D_CHANNEL_FUNCTION_FLAG_SIBLING | DMA2D_CHANNEL_FUNCTION_FLAG_TX_CSC;
    dma2d->trans.dma_chan_desc.specified_tx_channel_mask = 0;
    dma2d->trans.dma_chan_desc.specified_rx_channel_mask = 0;
    dma2d->trans.dma_chan_desc.user_config = (void*) &dma2d->trans;
    dma2d->trans.dma_chan_desc.on_job_picked = dma2d_m2m_transaction_on_picked;

    // Configure color space conversion (YUV420→RGB565)
    dma2d->tx_cvt.pre_scramble = 3;  // YUV420 to RGB565 mode
    dma2d->tx_cvt.tx_color_format = COLOR_TYPE_ID(COLOR_SPACE_YUV, COLOR_PIXEL_YUV420);
    dma2d->tx_cvt.rx_color_format = COLOR_TYPE_ID(COLOR_SPACE_RGB, COLOR_PIXEL_RGB565);
    dma2d->tx_cvt.yuv_sample = COLOR_CONV_STD_RGB_YUV_BT601;  // BT.601 for SD video
    dma2d->tx_cvt.yuv_range = COLOR_RANGE_LIMIT;  // Limited range [16-235]

    // Setup transfer configuration
    dma2d->trans.m2m_trans_desc.transfer_ability.data_burst_length = DMA2D_DATA_BURST_LENGTH_128;
    dma2d->trans.m2m_trans_desc.transfer_ability.desc_burst_en = true;
    dma2d->trans.m2m_trans_desc.transfer_ability.mb_size = DMA2D_MACRO_BLOCK_SIZE_NONE;
    dma2d->trans.m2m_trans_desc.tx_csc_config = &dma2d->tx_cvt;
    dma2d->trans.m2m_trans_desc.trans_eof_cb = dma2d_m2m_suc_eof_event_cb;
    dma2d->trans.m2m_trans_desc.user_data = (void *)dma2d->sema;

    // Initialize descriptors for YUV420→RGB565
    // Input: YUV420 = width * height * 1.5 bytes (Y plane + U/V planes)
    // Output: RGB565 = width * height * 2 bytes
    int src_size = width * height * 3 / 2;  // YUV420 size
    int dst_size = width * height * 2;      // RGB565 size

    // TX descriptor (input YUV420)
    dma2d_link_dscr_init(dma2d->tx_desc, nullptr, nullptr,
                         src_size >> 14, src_size >> 14,
                         src_size & 0x3FFF, src_size & 0x3FFF,
                         1, 0, DMA2D_DESCRIPTOR_PBYTE_1B0_PER_PIXEL,
                         DMA2D_DESCRIPTOR_BLOCK_RW_MODE_SINGLE, 0, 0);

    // RX descriptor (output RGB565)
    dma2d_link_dscr_init(dma2d->rx_desc, nullptr, nullptr,
                         0, dst_size >> 14,
                         0, dst_size & 0x3FFF,
                         0, 0, DMA2D_DESCRIPTOR_PBYTE_1B0_PER_PIXEL,
                         DMA2D_DESCRIPTOR_BLOCK_RW_MODE_SINGLE, 0, 0);

    // Set descriptor base addresses
    dma2d->trans.m2m_trans_desc.tx_desc_base_addr = (intptr_t)dma2d->tx_desc;
    dma2d->trans.m2m_trans_desc.rx_desc_base_addr = (intptr_t)dma2d->rx_desc;

    ESP_LOGI(TAG, "✓ 2D-DMA initialized for YUV420→RGB565 (%dx%d)", width, height);
    ESP_LOGI(TAG, "  TX descriptor: src_size=%d bytes", src_size);
    ESP_LOGI(TAG, "  RX descriptor: dst_size=%d bytes", dst_size);
    return ESP_OK;
}

// ============================================================================
// PPA initialization for YUV420→RGB565 (fallback)
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

    // Configure PPA for YUV420→RGB565
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

    ESP_LOGI(TAG, "✓ PPA initialized for YUV420→RGB565 (%dx%d)", width, height);
    return ESP_OK;
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t gmf_ppa_init() {
    if (g_gmf_ppa.initialized) {
        ESP_LOGW(TAG, "GMF PPA already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing GMF PPA (2D-DMA + PPA hardware)...");

    // Try 2D-DMA first (best performance - direct YUV420→RGB565 with hardware CSC)
    // If 2D-DMA fails, will fall back to PPA on first conversion
    g_gmf_ppa.use_dma2d = true;
    ESP_LOGI(TAG, "✓ GMF PPA ready (will try 2D-DMA, fallback to PPA if needed)");

    g_gmf_ppa.initialized = true;
    return ESP_OK;
}

esp_err_t gmf_ppa_convert_yuv420_to_rgb565(const uint8_t *yuv_in, uint8_t *rgb_out,
                                            uint16_t width, uint16_t height) {
    if (!g_gmf_ppa.initialized) {
        ESP_LOGE(TAG, "GMF PPA not initialized! Call gmf_ppa_init() first");
        return ESP_ERR_INVALID_STATE;
    }

    // Initialize for this resolution if needed
    if (g_gmf_ppa.width != width || g_gmf_ppa.height != height) {
        // Cleanup old resources
        if (g_gmf_ppa.ppa_handle) {
            ppa_unregister_client(g_gmf_ppa.ppa_handle);
            g_gmf_ppa.ppa_handle = nullptr;
        }

        esp_err_t ret;
        if (g_gmf_ppa.use_dma2d) {
            ret = init_dma2d_yuv420_to_rgb565(width, height);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "2D-DMA init failed: %s, falling back to PPA", esp_err_to_name(ret));
                g_gmf_ppa.use_dma2d = false;
                ret = init_ppa_yuv420_to_rgb565(width, height);
            }
        } else {
            ret = init_ppa_yuv420_to_rgb565(width, height);
        }

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize converter: %s", esp_err_to_name(ret));
            return ret;
        }

        g_gmf_ppa.width = width;
        g_gmf_ppa.height = height;
    }

    // Perform conversion
    if (g_gmf_ppa.use_dma2d) {
        // 2D-DMA conversion with hardware CSC (Color Space Conversion)
        dma2d_info_t *dma2d = &g_gmf_ppa.dma2d_info;
        int yuv_size = width * height * 3 / 2;  // YUV420
        int rgb_size = width * height * 2;      // RGB565

        // Flush cache for source and destination buffers (PSRAM→DMA)
        esp_cache_msync((void *)yuv_in, yuv_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        esp_cache_msync((void *)rgb_out, rgb_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

        // Set buffer pointers in descriptors
        dma2d->tx_desc->buffer = (void *)yuv_in;
        dma2d->rx_desc->buffer = (void *)rgb_out;

        // Sync descriptor changes to memory
        esp_cache_msync(dma2d->tx_desc, 64, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
        esp_cache_msync(dma2d->rx_desc, 64, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);

        // Enqueue DMA transfer
        esp_err_t ret = dma2d_enqueue(dma2d->handle, &dma2d->trans.dma_chan_desc,
                                       (dma2d_trans_t *)dma2d->trans.dma_trans_placeholder_head);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "2D-DMA enqueue failed: %s", esp_err_to_name(ret));
            return ret;
        }

        // Wait for DMA transfer to complete
        xSemaphoreTake(dma2d->sema, portMAX_DELAY);

        // Invalidate destination buffer cache (DMA→CPU)
        esp_cache_msync(rgb_out, rgb_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);

        return ESP_OK;
    } else {
        // PPA conversion
        g_gmf_ppa.ppa_config.in.buffer = (void*)yuv_in;
        g_gmf_ppa.ppa_config.out.buffer = (void*)rgb_out;
        g_gmf_ppa.ppa_config.out.buffer_size = width * height * 2;  // RGB565

        esp_err_t ret = ppa_do_scale_rotate_mirror(g_gmf_ppa.ppa_handle, &g_gmf_ppa.ppa_config);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "PPA conversion failed: %s", esp_err_to_name(ret));
        }
        return ret;
    }
}

void gmf_ppa_deinit() {
    if (!g_gmf_ppa.initialized) {
        return;
    }

    if (g_gmf_ppa.ppa_handle) {
        ppa_unregister_client(g_gmf_ppa.ppa_handle);
        g_gmf_ppa.ppa_handle = nullptr;
    }

    if (g_gmf_ppa.use_dma2d) {
        dma2d_info_t *dma2d = &g_gmf_ppa.dma2d_info;
        if (dma2d->handle) {
            dma2d_release_pool(dma2d->handle);
        }
        if (dma2d->tx_desc) {
            heap_caps_free(dma2d->tx_desc);
        }
        if (dma2d->rx_desc) {
            heap_caps_free(dma2d->rx_desc);
        }
        if (dma2d->sema) {
            vSemaphoreDelete(dma2d->sema);
        }
    }

    memset(&g_gmf_ppa, 0, sizeof(g_gmf_ppa));
    ESP_LOGI(TAG, "GMF PPA deinitialized");
}

#endif // CONFIG_IDF_TARGET_ESP32P4
