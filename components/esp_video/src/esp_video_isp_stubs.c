#include "sdkconfig.h"

#if CONFIG_ESP_VIDEO_ENABLE_ISP

#include <inttypes.h>

#include "esp_log.h"
#include "esp_video_device_internal.h"

static const char *TAG = "esp_video_isp_stub";

__attribute__((weak)) esp_err_t esp_video_isp_start_by_csi(const esp_video_csi_state_t *state,
                                                           const struct v4l2_format *format)
{
    uint32_t pix_format = 0;

    if (format) {
        if (format->type == V4L2_BUF_TYPE_VIDEO_CAPTURE ||
                format->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
            pix_format = format->fmt.pix.pixelformat;
        }
    }

    if (state && state->bypass_isp) {
        ESP_LOGW(TAG, "ISP pipeline disabled; bypassing processing (pix=0x%08" PRIx32 ")", pix_format);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "ISP pipeline required for pix=0x%08" PRIx32 ", but not available in this build", pix_format);
    return ESP_ERR_NOT_SUPPORTED;
}

__attribute__((weak)) esp_err_t esp_video_isp_stop(const esp_video_csi_state_t *state)
{
    (void)state;
    return ESP_OK;
}

__attribute__((weak)) esp_err_t esp_video_isp_enum_format(esp_video_csi_state_t *state,
                                                           uint32_t index,
                                                           uint32_t *pixel_format)
{
    if (!pixel_format) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!state) {
        return ESP_ERR_INVALID_STATE;
    }

    if (index == 0) {
        *pixel_format = state->in_fmt;
        return ESP_OK;
    }

    return ESP_ERR_NOT_SUPPORTED;
}

__attribute__((weak)) esp_err_t esp_video_isp_check_format(esp_video_csi_state_t *state,
                                                            const struct v4l2_format *format)
{
    (void)state;
    (void)format;
    return ESP_ERR_NOT_SUPPORTED;
}

#if CONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE
__attribute__((weak)) esp_err_t esp_video_create_isp_video_device(void)
{
    ESP_LOGW(TAG, "ISP video device not available; registering bypass stub");
    return ESP_OK;
}

__attribute__((weak)) esp_err_t esp_video_destroy_isp_video_device(void)
{
    ESP_LOGW(TAG, "ISP video device bypass stub destroyed");
    return ESP_OK;
}
#endif

#endif /* CONFIG_ESP_VIDEO_ENABLE_ISP */
