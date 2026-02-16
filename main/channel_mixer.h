/**
 * Channel Mixer
 * 
 * Maps Xbox controller inputs to RC channels with configurable:
 * - Channel assignment
 * - Expo curves
 * - Deadbands
 * - Endpoint adjustment
 * - Throttle/brake mixing options
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "xbox_receiver.h"
#include "crsf.h"

#ifdef __cplusplus
extern "C" {
#endif

// Standard RC channel assignments (AETR order)
typedef enum {
    RC_CH_AILERON = 0,   // Roll / Steering
    RC_CH_ELEVATOR = 1,  // Pitch (unused for car)
    RC_CH_THROTTLE = 2,  // Throttle
    RC_CH_RUDDER = 3,    // Yaw (unused for car)
    RC_CH_AUX1 = 4,      // Aux channels for switches
    RC_CH_AUX2 = 5,
    RC_CH_AUX3 = 6,
    RC_CH_AUX4 = 7,
    RC_CH_AUX5 = 8,
    RC_CH_AUX6 = 9,
    RC_CH_AUX7 = 10,
    RC_CH_AUX8 = 11,
    RC_CH_AUX9 = 12,
    RC_CH_AUX10 = 13,
    RC_CH_AUX11 = 14,
    RC_CH_AUX12 = 15,
} rc_channel_t;

// Throttle/brake mixing modes
typedef enum {
    // Separate channels: throttle on CH3, brake on CH4
    MIX_MODE_SEPARATE,
    
    // Combined: center = stop, forward = throttle, back = brake
    // (like a standard car ESC expects)
    MIX_MODE_COMBINED,
    
    // Throttle only, brake ignored
    MIX_MODE_THROTTLE_ONLY,
} throttle_mix_mode_t;

// Expo curve settings (0-100)
// Higher values = softer center response (less sensitive around center)
typedef struct {
    uint8_t steering;
    uint8_t throttle;
} expo_settings_t;

// Deadband settings (0-50, percentage of center range)
typedef struct {
    uint8_t steering;
    uint8_t throttle;
} deadband_settings_t;

// Full mixer configuration
typedef struct {
    throttle_mix_mode_t throttle_mode;
    expo_settings_t expo;
    deadband_settings_t deadband;
    
    // Steering settings
    bool steering_invert;
    uint8_t steering_endpoint_left;   // 0-100%
    uint8_t steering_endpoint_right;  // 0-100%
    
    // Throttle settings  
    bool throttle_invert;
    uint8_t throttle_endpoint;        // 0-100%
    uint8_t brake_endpoint;           // 0-100%
    
    // ARM channel (high when controller connected and ready)
    rc_channel_t arm_channel;
    
    // Button → channel mappings
    rc_channel_t paddle_left_channel;   // Usually upshift
    rc_channel_t paddle_right_channel;  // Usually downshift
    rc_channel_t button_a_channel;
    rc_channel_t button_b_channel;
    rc_channel_t button_x_channel;
    rc_channel_t button_y_channel;
} mixer_config_t;

// Default configuration for Xbox 360 racing wheel
#define MIXER_CONFIG_DEFAULT() { \
    .throttle_mode = MIX_MODE_COMBINED, \
    .expo = { .steering = 0, .throttle = 0 }, \
    .deadband = { .steering = 3, .throttle = 2 }, \
    .steering_invert = false, \
    .steering_endpoint_left = 27, \
    .steering_endpoint_right = 28, \
    .throttle_invert = false, \
    .throttle_endpoint = 46, \
    .brake_endpoint = 28, \
    .arm_channel = RC_CH_AUX1, \
    .paddle_left_channel = RC_CH_AUX2, \
    .paddle_right_channel = RC_CH_AUX3, \
    .button_a_channel = RC_CH_AUX4, \
    .button_b_channel = RC_CH_AUX5, \
    .button_x_channel = RC_CH_AUX6, \
    .button_y_channel = RC_CH_AUX7, \
}

/**
 * Initialize mixer with configuration
 */
esp_err_t mixer_init(const mixer_config_t *config);

/**
 * Update mixer configuration at runtime
 */
void mixer_set_config(const mixer_config_t *config);

/**
 * Get current mixer configuration
 */
void mixer_get_config(mixer_config_t *config);

/**
 * Process Xbox controller state and output to CRSF channels
 * 
 * @param xbox_state Input from Xbox controller
 * @param crsf_out Output channel data for CRSF
 */
void mixer_process(const xbox_controller_state_t *xbox_state, crsf_channels_t *crsf_out);

/**
 * Apply expo curve to an axis value
 * 
 * @param value Input value (-32768 to 32767)
 * @param expo Expo setting (0-100)
 * @return Curved value
 */
int16_t mixer_apply_expo(int16_t value, uint8_t expo);

/**
 * Apply deadband to an axis value
 * 
 * @param value Input value (-32768 to 32767)  
 * @param deadband Deadband percentage (0-50)
 * @return Value with deadband applied
 */
int16_t mixer_apply_deadband(int16_t value, uint8_t deadband);

#ifdef __cplusplus
}
#endif
