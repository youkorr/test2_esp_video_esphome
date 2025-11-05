/*
 * Stub definitions for sensor and IPA detection arrays
 *
 * In ESP-IDF CMake builds, these symbols are created by linker fragments.
 * For PlatformIO builds, we define them to represent an empty array.
 *
 * The trick: we place "end" before "start" in memory using section ordering,
 * so the loop condition (p < &end) is immediately false.
 */

#include "esp_cam_sensor_detect.h"
#include "esp_ipa_detect.h"

/*
 * Define end marker FIRST (using section .1) and start marker SECOND (using section .2)
 * This ensures &start >= &end, making the loop condition false immediately.
 */

/* End marker - comes first in memory (section .1 < .2) */
__attribute__((section(".rodata.esp_cam_detect.1_end")))
__attribute__((used))
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_end = {
    .detect = NULL,
    .port = 0,
    .sccb_addr = 0
};

/* Start marker - comes after in memory (section .2 > .1) */
__attribute__((section(".rodata.esp_cam_detect.2_start")))
__attribute__((used))
esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start = {
    .detect = NULL,
    .port = 0,
    .sccb_addr = 0
};

/* Motor detection arrays (if camera motor support is enabled) */
#ifdef CONFIG_ESP_VIDEO_ENABLE_CAMERA_MOTOR_CONTROLLER
typedef struct {
    void *detect;
    uint8_t sccb_addr;
    uint8_t port;
} esp_cam_motor_detect_fn_t;

/* End before start for motor array too */
__attribute__((section(".rodata.esp_cam_motor.1_end")))
__attribute__((used))
esp_cam_motor_detect_fn_t __esp_cam_motor_detect_fn_array_end = {
    .detect = NULL,
    .port = 0,
    .sccb_addr = 0
};

__attribute__((section(".rodata.esp_cam_motor.2_start")))
__attribute__((used))
esp_cam_motor_detect_fn_t __esp_cam_motor_detect_fn_array_start = {
    .detect = NULL,
    .port = 0,
    .sccb_addr = 0
};
#endif

/* ========================================================================
 * IPA (Image Processing Algorithm) detection arrays
 * ======================================================================== */

/* IPA end marker - comes first in memory */
__attribute__((section(".rodata.esp_ipa_detect.1_end")))
__attribute__((used))
esp_ipa_detect_t __esp_ipa_detect_array_end = {
    .name = NULL,
    .detect = NULL
};

/* IPA start marker - comes after in memory */
__attribute__((section(".rodata.esp_ipa_detect.2_start")))
__attribute__((used))
esp_ipa_detect_t __esp_ipa_detect_array_start = {
    .name = NULL,
    .detect = NULL
};
