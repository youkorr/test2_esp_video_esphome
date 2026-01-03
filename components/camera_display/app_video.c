/*
 * app_video.c - Low-level esp_video camera streaming implementation
 *
 * Based on Waveshare ESP32-P4 Camera implementation
 * https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B
 *
 * Architecture:
 * 1. Allocate DMA-aligned buffers (heap_caps_aligned_alloc)
 * 2. Configure MIPI CSI camera via V4L2 ioctls
 * 3. Dedicated streaming task: DQBUF → callback → QBUF
 * 4. Zero-copy: buffers shared between camera DMA and display
 */

#include "app_video.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>

#include "esp_cam_sensor.h"
#include "esp_cam_sensor_detect.h"

static const char *TAG = "app_video";

// Camera device path
#define EXAMPLE_CAM_DEV_PATH ESP_VIDEO_MIPI_CSI_DEVICE_NAME

// Event bits for stream task control
#define VIDEO_TASK_DELETE      BIT0
#define VIDEO_TASK_DELETE_DONE BIT1

// Buffer count limits
#define MIN_BUFFER_COUNT 2
#define MAX_BUFFER_COUNT 6

// Module state
static struct {
    uint8_t *buffers[MAX_BUFFER_COUNT];      // Frame buffer pointers
    uint32_t buffer_count;                    // Number of allocated buffers
    uint32_t buffer_size;                     // Size of each buffer

    TaskHandle_t stream_task;                 // Streaming task handle
    EventGroupHandle_t event_group;           // Task control events

    app_video_frame_operation_cb_t frame_cb;  // User callback

    uint32_t frame_width;                     // Current frame width
    uint32_t frame_height;                    // Current frame height

    esp_cam_sensor_device_t *cam_dev;         // Camera sensor device
} s_video = {0};

/**
 * Convert pixel format enum to V4L2 format code
 */
static uint32_t video_fmt_to_v4l2(video_fmt_t fmt) {
    switch (fmt) {
        case APP_VIDEO_FMT_RAW8:   return V4L2_PIX_FMT_SRGGB8;
        case APP_VIDEO_FMT_RAW10:  return V4L2_PIX_FMT_SRGGB10;
        case APP_VIDEO_FMT_GRAY:   return V4L2_PIX_FMT_GREY;
        case APP_VIDEO_FMT_RGB565: return V4L2_PIX_FMT_RGB565;
        case APP_VIDEO_FMT_RGB888: return V4L2_PIX_FMT_RGB24;
        case APP_VIDEO_FMT_YUV422P: return V4L2_PIX_FMT_YUV422P;
        case APP_VIDEO_FMT_YUV420: return V4L2_PIX_FMT_YUV420;
        default: return V4L2_PIX_FMT_RGB565;
    }
}

/**
 * Calculate buffer size based on format and dimensions
 */
size_t app_video_get_buf_size(uint32_t width, uint32_t height, video_fmt_t fmt) {
    switch (fmt) {
        case APP_VIDEO_FMT_RAW8:
        case APP_VIDEO_FMT_GRAY:
            return width * height;
        case APP_VIDEO_FMT_RAW10:
            return width * height * 2;  // 10-bit unpacked to 16-bit
        case APP_VIDEO_FMT_RGB565:
            return width * height * 2;
        case APP_VIDEO_FMT_RGB888:
            return width * height * 3;
        case APP_VIDEO_FMT_YUV422P:
            return width * height * 2;
        case APP_VIDEO_FMT_YUV420:
            return width * height * 3 / 2;
        default:
            return width * height * 2;  // Default to RGB565
    }
}

/**
 * Initialize camera sensor and esp_video system
 */
esp_err_t app_video_main(const app_video_config_t *config) {
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "Initializing camera: %s (%ux%u)",
             config->sensor_name, config->width, config->height);

    // Auto-detect and initialize camera sensor
    esp_cam_sensor_config_t cam_config = {
        .sccb_handle = config->i2c_bus,
        .reset_pin = -1,      // Handled by BSP
        .pwdn_pin = -1,       // Handled by BSP
        .xclk_pin = -1,       // Handled by BSP
        .xclk_freq_hz = 24000000,
    };

    // Detect camera sensor
    s_video.cam_dev = esp_cam_sensor_detect(&cam_config);
    if (s_video.cam_dev == NULL) {
        ESP_LOGE(TAG, "Failed to detect camera sensor");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Camera sensor detected: %s", s_video.cam_dev->name);

    // Create event group for task control
    s_video.event_group = xEventGroupCreate();
    if (s_video.event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

/**
 * Open camera device and configure format
 */
int app_video_open(uint32_t width, uint32_t height, video_fmt_t fmt, bool mirror_x, bool mirror_y) {
    int fd = open(EXAMPLE_CAM_DEV_PATH, O_RDWR);
    if (fd < 0) {
        ESP_LOGE(TAG, "Failed to open camera device: %s", EXAMPLE_CAM_DEV_PATH);
        return -1;
    }

    ESP_LOGI(TAG, "Camera device opened: fd=%d", fd);

    // Set pixel format
    struct v4l2_format v_fmt = {0};
    v_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v_fmt.fmt.pix.width = width;
    v_fmt.fmt.pix.height = height;
    v_fmt.fmt.pix.pixelformat = video_fmt_to_v4l2(fmt);
    v_fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (ioctl(fd, VIDIOC_S_FMT, &v_fmt) < 0) {
        ESP_LOGE(TAG, "Failed to set format");
        close(fd);
        return -1;
    }

    ESP_LOGI(TAG, "Format set: %ux%u, pixelformat=%c%c%c%c",
             v_fmt.fmt.pix.width, v_fmt.fmt.pix.height,
             (char)(v_fmt.fmt.pix.pixelformat),
             (char)(v_fmt.fmt.pix.pixelformat >> 8),
             (char)(v_fmt.fmt.pix.pixelformat >> 16),
             (char)(v_fmt.fmt.pix.pixelformat >> 24));

    // Apply mirror settings if needed
    if (mirror_x || mirror_y) {
        struct v4l2_control ctrl = {0};
        if (mirror_x) {
            ctrl.id = V4L2_CID_HFLIP;
            ctrl.value = 1;
            ioctl(fd, VIDIOC_S_CTRL, &ctrl);
        }
        if (mirror_y) {
            ctrl.id = V4L2_CID_VFLIP;
            ctrl.value = 1;
            ioctl(fd, VIDIOC_S_CTRL, &ctrl);
        }
        ESP_LOGI(TAG, "Mirror applied: H=%d, V=%d", mirror_x, mirror_y);
    }

    // Store dimensions
    s_video.frame_width = width;
    s_video.frame_height = height;

    return fd;
}

/**
 * Allocate DMA-aligned frame buffers
 */
esp_err_t app_video_set_bufs(int fd, uint32_t buffer_count) {
    if (buffer_count < MIN_BUFFER_COUNT || buffer_count > MAX_BUFFER_COUNT) {
        ESP_LOGE(TAG, "Invalid buffer count: %lu (min=%d, max=%d)",
                 buffer_count, MIN_BUFFER_COUNT, MAX_BUFFER_COUNT);
        return ESP_ERR_INVALID_ARG;
    }

    // Get cache line size for alignment
    size_t cache_line_size = 0;
    if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &cache_line_size) != ESP_OK) {
        cache_line_size = 64;  // Default alignment
    }

    ESP_LOGI(TAG, "Allocating %lu buffers (cache alignment: %zu bytes)",
             buffer_count, cache_line_size);

    // Request buffers from V4L2
    struct v4l2_requestbuffers req = {0};
    req.count = buffer_count;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_USERPTR;  // We provide our own buffers

    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        ESP_LOGE(TAG, "Failed to request buffers");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "V4L2 accepted %lu buffers", req.count);

    // Calculate buffer size
    struct v4l2_format v_fmt = {0};
    v_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_G_FMT, &v_fmt) < 0) {
        ESP_LOGE(TAG, "Failed to get format");
        return ESP_FAIL;
    }

    size_t buf_size = v_fmt.fmt.pix.sizeimage;
    ESP_LOGI(TAG, "Buffer size: %zu bytes", buf_size);

    // Allocate DMA-aligned buffers in SPIRAM
    for (uint32_t i = 0; i < req.count; i++) {
        s_video.buffers[i] = (uint8_t *)heap_caps_aligned_alloc(
            cache_line_size, buf_size, MALLOC_CAP_SPIRAM
        );

        if (s_video.buffers[i] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate buffer %lu", i);
            // Free previously allocated buffers
            for (uint32_t j = 0; j < i; j++) {
                heap_caps_free(s_video.buffers[j]);
            }
            return ESP_ERR_NO_MEM;
        }

        ESP_LOGI(TAG, "  Buffer[%lu]: %p", i, s_video.buffers[i]);

        // Queue buffer to V4L2
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_USERPTR;
        buf.index = i;
        buf.m.userptr = (unsigned long)s_video.buffers[i];
        buf.length = buf_size;

        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            ESP_LOGE(TAG, "Failed to queue buffer %lu", i);
            return ESP_FAIL;
        }
    }

    s_video.buffer_count = req.count;
    s_video.buffer_size = buf_size;

    ESP_LOGI(TAG, "All buffers allocated and queued");
    return ESP_OK;
}

/**
 * Get buffer pointers
 */
esp_err_t app_video_get_bufs(uint8_t **bufs, uint32_t buf_count) {
    if (buf_count > s_video.buffer_count) {
        ESP_LOGE(TAG, "Requested %lu buffers, but only %lu available",
                 buf_count, s_video.buffer_count);
        return ESP_ERR_INVALID_ARG;
    }

    for (uint32_t i = 0; i < buf_count; i++) {
        bufs[i] = s_video.buffers[i];
    }

    return ESP_OK;
}

/**
 * Register frame callback
 */
void app_video_register_frame_operation_cb(app_video_frame_operation_cb_t cb) {
    s_video.frame_cb = cb;
    ESP_LOGI(TAG, "Frame callback registered: %p", cb);
}

/**
 * Video streaming task - runs on dedicated CPU core
 *
 * This is the heart of the Waveshare architecture:
 * Loop: VIDIOC_DQBUF → user callback → VIDIOC_QBUF
 */
static void video_stream_task(void *arg) {
    int fd = (int)(intptr_t)arg;

    ESP_LOGI(TAG, "Stream task started on core %d", xPortGetCoreID());

    // Start streaming
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        ESP_LOGE(TAG, "Failed to start streaming");
        xEventGroupSetBits(s_video.event_group, VIDEO_TASK_DELETE_DONE);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Streaming started");

    uint32_t frame_count = 0;
    uint32_t last_log_time = 0;

    while (1) {
        // Check for stop signal
        EventBits_t bits = xEventGroupGetBits(s_video.event_group);
        if (bits & VIDEO_TASK_DELETE) {
            ESP_LOGI(TAG, "Stop signal received");
            break;
        }

        // Dequeue filled buffer from camera
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_USERPTR;

        if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
            ESP_LOGE(TAG, "Failed to dequeue buffer");
            continue;
        }

        // Call user callback with frame data
        if (s_video.frame_cb != NULL) {
            s_video.frame_cb(
                (uint8_t *)buf.m.userptr,
                buf.index,
                s_video.frame_width,
                s_video.frame_height,
                buf.bytesused
            );
        }

        // Requeue buffer for next capture
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            ESP_LOGE(TAG, "Failed to requeue buffer");
        }

        // FPS logging every 100 frames
        frame_count++;
        if (frame_count % 100 == 0) {
            uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (last_log_time > 0) {
                float fps = 100000.0f / (now - last_log_time);
                ESP_LOGI(TAG, "Streaming: %lu frames (%.1f FPS)", frame_count, fps);
            }
            last_log_time = now;
        }
    }

    // Stop streaming
    if (ioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
        ESP_LOGE(TAG, "Failed to stop streaming");
    }

    ESP_LOGI(TAG, "Stream task stopped (total frames: %lu)", frame_count);
    xEventGroupSetBits(s_video.event_group, VIDEO_TASK_DELETE_DONE);
    vTaskDelete(NULL);
}

/**
 * Start streaming task on specified CPU core
 */
esp_err_t app_video_stream_task_start(int fd, int core_id, uint32_t width, uint32_t height) {
    if (s_video.stream_task != NULL) {
        ESP_LOGW(TAG, "Stream task already running");
        return ESP_ERR_INVALID_STATE;
    }

    xEventGroupClearBits(s_video.event_group, VIDEO_TASK_DELETE | VIDEO_TASK_DELETE_DONE);

    BaseType_t ret = xTaskCreatePinnedToCore(
        video_stream_task,
        "video_stream",
        4096,                    // Stack size
        (void *)(intptr_t)fd,    // Pass fd as argument
        5,                       // Priority
        &s_video.stream_task,
        core_id                  // Pin to specified core
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create stream task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Stream task created on core %d", core_id);
    return ESP_OK;
}

/**
 * Stop streaming task
 */
esp_err_t app_video_stream_task_stop(void) {
    if (s_video.stream_task == NULL) {
        ESP_LOGW(TAG, "Stream task not running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping stream task...");
    xEventGroupSetBits(s_video.event_group, VIDEO_TASK_DELETE);

    return ESP_OK;
}

/**
 * Wait for streaming task to stop (blocking)
 */
esp_err_t app_video_stream_wait_stop(void) {
    if (s_video.stream_task == NULL) {
        return ESP_OK;
    }

    // Wait for task to signal completion
    EventBits_t bits = xEventGroupWaitBits(
        s_video.event_group,
        VIDEO_TASK_DELETE_DONE,
        pdTRUE,  // Clear on exit
        pdFALSE, // Wait for any bit
        pdMS_TO_TICKS(5000)  // 5 second timeout
    );

    if (!(bits & VIDEO_TASK_DELETE_DONE)) {
        ESP_LOGW(TAG, "Timeout waiting for stream task to stop");
        return ESP_ERR_TIMEOUT;
    }

    s_video.stream_task = NULL;
    ESP_LOGI(TAG, "Stream task stopped");

    return ESP_OK;
}
