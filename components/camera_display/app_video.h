/*
 * app_video.h - Low-level esp_video camera management
 *
 * Based on Waveshare ESP32-P4 Camera implementation
 * https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B
 *
 * This module manages direct V4L2/esp_video operations:
 * - Buffer allocation (DMA-aligned)
 * - MIPI CSI camera streaming
 * - Frame callback system
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_video.h"
#include "driver/i2c_master.h"

/**
 * Pixel format types supported by camera
 */
typedef enum {
    APP_VIDEO_FMT_RAW8,
    APP_VIDEO_FMT_RAW10,
    APP_VIDEO_FMT_GRAY,
    APP_VIDEO_FMT_RGB565,
    APP_VIDEO_FMT_RGB888,
    APP_VIDEO_FMT_YUV422P,
    APP_VIDEO_FMT_YUV420,
} video_fmt_t;

/**
 * Frame callback - called for each captured frame
 *
 * @param camera_buf       Pointer to frame buffer data
 * @param camera_buf_index Buffer index (0 to buffer_count-1)
 * @param camera_buf_width Frame width in pixels
 * @param camera_buf_height Frame height in pixels
 * @param camera_buf_len   Buffer size in bytes
 */
typedef void (*app_video_frame_operation_cb_t)(
    uint8_t *camera_buf,
    uint8_t camera_buf_index,
    uint32_t camera_buf_width,
    uint32_t camera_buf_height,
    size_t camera_buf_len
);

/**
 * Camera configuration structure
 */
typedef struct {
    const char *sensor_name;        // Sensor model: "sc202cs", "ov5647", "ov02c10"
    uint32_t width;                 // Frame width (e.g., 800)
    uint32_t height;                // Frame height (e.g., 600)
    video_fmt_t pixel_format;       // Pixel format (typically RGB565)
    uint32_t buffer_count;          // Number of frame buffers (2-6)
    i2c_master_bus_handle_t i2c_bus; // I2C bus handle (optional, can be NULL)
} app_video_config_t;

/**
 * Initialize esp_video camera system
 *
 * @param config Camera configuration
 * @return ESP_OK on success
 */
esp_err_t app_video_main(const app_video_config_t *config);

/**
 * Open camera device and configure format
 *
 * @param width Frame width
 * @param height Frame height
 * @param fmt Pixel format
 * @param mirror_x Horizontal mirror
 * @param mirror_y Vertical mirror
 * @return File descriptor on success, -1 on error
 */
int app_video_open(uint32_t width, uint32_t height, video_fmt_t fmt, bool mirror_x, bool mirror_y);

/**
 * Allocate and setup frame buffers
 *
 * @param fd Camera file descriptor
 * @param buffer_count Number of buffers to allocate
 * @return ESP_OK on success
 */
esp_err_t app_video_set_bufs(int fd, uint32_t buffer_count);

/**
 * Get allocated frame buffer pointers
 *
 * @param bufs Output array of buffer pointers
 * @param buf_count Number of buffers
 * @return ESP_OK on success
 */
esp_err_t app_video_get_bufs(uint8_t **bufs, uint32_t buf_count);

/**
 * Calculate buffer size for given format and dimensions
 *
 * @param width Frame width
 * @param height Frame height
 * @param fmt Pixel format
 * @return Buffer size in bytes
 */
size_t app_video_get_buf_size(uint32_t width, uint32_t height, video_fmt_t fmt);

/**
 * Start streaming task on specified CPU core
 *
 * @param fd Camera file descriptor
 * @param core_id CPU core (0 or 1)
 * @param width Frame width
 * @param height Frame height
 * @return ESP_OK on success
 */
esp_err_t app_video_stream_task_start(int fd, int core_id, uint32_t width, uint32_t height);

/**
 * Stop streaming task
 *
 * @return ESP_OK on success
 */
esp_err_t app_video_stream_task_stop(void);

/**
 * Register frame operation callback
 *
 * @param cb Callback function to be called for each frame
 */
void app_video_register_frame_operation_cb(app_video_frame_operation_cb_t cb);

/**
 * Wait for streaming task to stop (blocking)
 *
 * @return ESP_OK on success
 */
esp_err_t app_video_stream_wait_stop(void);

#ifdef __cplusplus
}
#endif
