/*
 * Stub definitions for sensor detection arrays
 *
 * In ESP-IDF CMake builds, these symbols are created by linker fragments.
 * For PlatformIO builds, we define them as empty arrays where start == end.
 */

#include "esp_cam_sensor_detect.h"

/*
 * Define an empty array for sensor detection functions.
 * The start and end symbols point to the same location, indicating an empty array.
 */
static esp_cam_sensor_detect_fn_t __empty_sensor_array[0] __attribute__((used));

__attribute__((weak, alias("__empty_sensor_array")))
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start[];

__attribute__((weak, alias("__empty_sensor_array")))
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_end[];

/* Motor detection arrays (if camera motor support is enabled) */
#ifdef CONFIG_ESP_VIDEO_ENABLE_CAMERA_MOTOR_CONTROLLER
typedef struct {
    void *detect;
    uint8_t sccb_addr;
    uint8_t port;
} esp_cam_motor_detect_fn_t;

static esp_cam_motor_detect_fn_t __empty_motor_array[0] __attribute__((used));

__attribute__((weak, alias("__empty_motor_array")))
esp_cam_motor_detect_fn_t __esp_cam_motor_detect_fn_array_start[];

__attribute__((weak, alias("__empty_motor_array")))
esp_cam_motor_detect_fn_t __esp_cam_motor_detect_fn_array_end[];
#endif
