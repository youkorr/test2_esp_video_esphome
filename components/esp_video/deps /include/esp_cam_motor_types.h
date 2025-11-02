#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file esp_cam_motor_types.h
 * @brief Définition minimale factice pour compatibilité ESP-Video/ESPHome.
 * 
 * Ce fichier est un stub : il permet la compilation même si
 * le support du moteur de lentille (focus motor) n’est pas utilisé.
 * Les structures et énumérations ici sont placeholders.
 */

// Type de moteur supporté
typedef enum {
    ESP_CAM_MOTOR_TYPE_NONE = 0,   ///< Aucun moteur
    ESP_CAM_MOTOR_TYPE_VCM,        ///< Voice Coil Motor (focus)
    ESP_CAM_MOTOR_TYPE_STEP,       ///< Moteur pas-à-pas
} esp_cam_motor_type_t;

// Configuration générique
typedef struct {
    esp_cam_motor_type_t type;     ///< Type de moteur
    int gpio_in1;                  ///< Broche IN1
    int gpio_in2;                  ///< Broche IN2 (optionnelle)
    int pwm_freq_hz;               ///< Fréquence PWM pour contrôle
} esp_cam_motor_config_t;

// Handle (factice)
typedef struct esp_cam_motor_t *esp_cam_motor_handle_t;

// Fonctions factices
static inline int esp_cam_motor_new(const esp_cam_motor_config_t *, esp_cam_motor_handle_t *) { return 0; }
static inline int esp_cam_motor_del(esp_cam_motor_handle_t) { return 0; }
static inline int esp_cam_motor_move(esp_cam_motor_handle_t, int steps) { (void)steps; return 0; }
static inline int esp_cam_motor_home(esp_cam_motor_handle_t) { return 0; }

#ifdef __cplusplus
}
#endif
