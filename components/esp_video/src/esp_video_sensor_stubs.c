/*
 * Stub definitions for sensor detection arrays
 *
 * In ESP-IDF CMake builds, these symbols are created by linker fragments.
 * For PlatformIO builds, we define them to represent an empty array.
 */

#include "esp_cam_sensor_detect.h"

/*
 * Define start and end markers for sensor detection array.
 * They point to the same dummy object, representing an empty array.
 * When code does: for (p = &start; p < &end; ++p), the loop won't execute.
 */
static const esp_cam_sensor_detect_fn_t __empty_sensor_marker = {
    .detect = NULL,
    .port = 0,
    .sccb_addr = 0
};

__attribute__((weak, alias("__empty_sensor_marker")))
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start;

__attribute__((weak, alias("__empty_sensor_marker")))
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_end;

/* Motor detection arrays (if camera motor support is enabled) */
#ifdef CONFIG_ESP_VIDEO_ENABLE_CAMERA_MOTOR_CONTROLLER
typedef struct {
    void *detect;
    uint8_t sccb_addr;
    uint8_t port;
} esp_cam_motor_detect_fn_t;

static const esp_cam_motor_detect_fn_t __empty_motor_marker = {
    .detect = NULL,
    .port = 0,
    .sccb_addr = 0
};

__attribute__((weak, alias("__empty_motor_marker")))
esp_cam_motor_detect_fn_t __esp_cam_motor_detect_fn_array_start;

__attribute__((weak, alias("__empty_motor_marker")))
esp_cam_motor_detect_fn_t __esp_cam_motor_detect_fn_array_end;
#endif
