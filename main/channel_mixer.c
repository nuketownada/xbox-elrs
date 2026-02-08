/**
 * Channel Mixer Implementation
 * 
 * Handles the transformation from Xbox controller inputs to RC channels,
 * including curves, deadbands, and mixing.
 */

#include <string.h>
#include <math.h>
#include "esp_log.h"

#include "channel_mixer.h"

static const char *TAG = "mixer";

static mixer_config_t s_config;

// ============================================================================
// Math helpers
// ============================================================================

/**
 * Apply expo curve to axis value
 * 
 * Expo formula: output = input * (1 - expo) + input^3 * expo
 * Where expo is normalized to -1.0 to 1.0
 * 
 * Negative expo = softer around center (good for steering)
 * Positive expo = sharper around center
 */
int16_t mixer_apply_expo(int16_t value, int8_t expo)
{
    if (expo == 0) {
        return value;
    }
    
    // Normalize input to -1.0 to 1.0
    float input = (float)value / 32768.0f;
    float exp_factor = (float)expo / 100.0f;
    
    // Cubic expo curve
    float linear = input;
    float cubic = input * input * input;
    float output = linear * (1.0f - fabsf(exp_factor)) + cubic * exp_factor;
    
    // Back to int16 range
    return (int16_t)(output * 32767.0f);
}

/**
 * Apply deadband to axis value
 * 
 * Removes small values around center and rescales the rest.
 */
int16_t mixer_apply_deadband(int16_t value, uint8_t deadband)
{
    if (deadband == 0) {
        return value;
    }
    
    // Deadband as percentage of half-range
    int32_t db_threshold = (32768 * deadband) / 100;
    
    if (value > -db_threshold && value < db_threshold) {
        return 0;
    }
    
    // Rescale remaining range
    if (value >= db_threshold) {
        int32_t scaled = ((int32_t)(value - db_threshold) * 32767) / (32768 - db_threshold);
        return (int16_t)scaled;
    } else {
        int32_t scaled = ((int32_t)(value + db_threshold) * 32767) / (32768 - db_threshold);
        return (int16_t)scaled;
    }
}

/**
 * Apply endpoint scaling
 * 
 * Scales the output range by a percentage.
 */
static int16_t apply_endpoint(int16_t value, uint8_t endpoint_percent)
{
    if (endpoint_percent >= 100) {
        return value;
    }
    
    return (int16_t)(((int32_t)value * endpoint_percent) / 100);
}

// ============================================================================
// Public API  
// ============================================================================

esp_err_t mixer_init(const mixer_config_t *config)
{
    if (config == NULL) {
        // Use defaults
        mixer_config_t defaults = MIXER_CONFIG_DEFAULT();
        memcpy(&s_config, &defaults, sizeof(mixer_config_t));
    } else {
        memcpy(&s_config, config, sizeof(mixer_config_t));
    }
    
    ESP_LOGI(TAG, "Mixer initialized, throttle mode: %s",
        s_config.throttle_mode == MIX_MODE_COMBINED ? "combined" :
        s_config.throttle_mode == MIX_MODE_SEPARATE ? "separate" : "throttle-only");
    
    return ESP_OK;
}

void mixer_set_config(const mixer_config_t *config)
{
    if (config != NULL) {
        memcpy(&s_config, config, sizeof(mixer_config_t));
    }
}

void mixer_get_config(mixer_config_t *config)
{
    if (config != NULL) {
        memcpy(config, &s_config, sizeof(mixer_config_t));
    }
}

void mixer_process(const xbox_controller_state_t *xbox_state, crsf_channels_t *crsf_out)
{
    // Initialize all channels to center
    for (int i = 0; i < CRSF_NUM_CHANNELS; i++) {
        crsf_out->ch[i] = CRSF_CHANNEL_MID;
    }
    
    if (!xbox_state->connected) {
        // Failsafe: everything centered except throttle
        crsf_out->ch[RC_CH_THROTTLE] = CRSF_CHANNEL_MIN;
        return;
    }
    
    // ========================================================================
    // Steering (Aileron channel)
    // Racing wheel steering maps to left_stick_x
    // ========================================================================
    
    int16_t steering = xbox_state->left_stick_x;
    
    // Racing wheel reports inverted magnitude: center=±max, full turn=0
    // Transform to standard RC axis: center=0, full turn=±max
    if (steering >= 0) {
        steering = 32767 - steering;
    } else {
        steering = -32767 - steering;
    }
    
    // Apply deadband
    steering = mixer_apply_deadband(steering, s_config.deadband.steering);
    
    // Apply expo
    steering = mixer_apply_expo(steering, s_config.expo.steering);
    
    // Apply invert
    if (s_config.steering_invert) {
        steering = -steering;
    }
    
    // Apply endpoints (asymmetric for steering)
    if (steering >= 0) {
        steering = apply_endpoint(steering, s_config.steering_endpoint_right);
    } else {
        steering = apply_endpoint(steering, s_config.steering_endpoint_left);
    }
    
    crsf_out->ch[RC_CH_AILERON] = crsf_scale_axis(steering);
    
    // ========================================================================
    // Throttle / Brake
    // Right trigger = throttle, Left trigger = brake
    // ========================================================================
    
    uint8_t throttle_raw = xbox_state->right_trigger;
    uint8_t brake_raw = xbox_state->left_trigger;
    
    switch (s_config.throttle_mode) {
        case MIX_MODE_COMBINED: {
            // Combined channel: center = stop, forward = throttle, back = brake
            // Map to: CRSF_CHANNEL_MID ± (throttle - brake)
            
            int16_t combined = (int16_t)throttle_raw - (int16_t)brake_raw;
            
            // Scale from -255..255 to -32768..32767
            int16_t scaled = (combined * 32767) / 255;
            
            // Apply expo
            scaled = mixer_apply_expo(scaled, s_config.expo.throttle);
            
            // Apply asymmetric endpoints (throttle_endpoint for fwd, brake_endpoint for rev)
            if (scaled >= 0) {
                scaled = apply_endpoint(scaled, s_config.throttle_endpoint);
            } else {
                scaled = apply_endpoint(scaled, s_config.brake_endpoint);
            }
            
            // Apply invert
            if (s_config.throttle_invert) {
                scaled = -scaled;
            }
            
            crsf_out->ch[RC_CH_THROTTLE] = crsf_scale_axis(scaled);
            break;
        }
        
        case MIX_MODE_SEPARATE: {
            // Separate channels: throttle on CH3, brake on CH4 (rudder)
            
            // Throttle: 0-255 -> CRSF min to max
            uint8_t throttle_scaled = (throttle_raw * s_config.throttle_endpoint) / 100;
            crsf_out->ch[RC_CH_THROTTLE] = crsf_scale_trigger(
                s_config.throttle_invert ? (255 - throttle_scaled) : throttle_scaled
            );
            
            // Brake: 0-255 -> CRSF min to max
            uint8_t brake_scaled = (brake_raw * s_config.brake_endpoint) / 100;
            crsf_out->ch[RC_CH_RUDDER] = crsf_scale_trigger(brake_scaled);
            break;
        }
        
        case MIX_MODE_THROTTLE_ONLY: {
            // Just throttle, ignore brake
            uint8_t throttle_scaled = (throttle_raw * s_config.throttle_endpoint) / 100;
            crsf_out->ch[RC_CH_THROTTLE] = crsf_scale_trigger(
                s_config.throttle_invert ? (255 - throttle_scaled) : throttle_scaled
            );
            break;
        }
    }
    
    // ========================================================================
    // Buttons to aux channels
    // ========================================================================
    
    // Paddle shifters (often A/B on racing wheels, or bumpers)
    // Some wheels use LB/RB for paddles, some use A/B
    // We'll map both - user can configure which channel matters
    crsf_out->ch[s_config.paddle_left_channel] = crsf_scale_switch(
        xbox_state->buttons.lb || xbox_state->buttons.a
    );
    crsf_out->ch[s_config.paddle_right_channel] = crsf_scale_switch(
        xbox_state->buttons.rb || xbox_state->buttons.b
    );
    
    // Additional buttons
    if (s_config.button_a_channel < CRSF_NUM_CHANNELS) {
        crsf_out->ch[s_config.button_a_channel] = crsf_scale_switch(xbox_state->buttons.a);
    }
    if (s_config.button_b_channel < CRSF_NUM_CHANNELS) {
        crsf_out->ch[s_config.button_b_channel] = crsf_scale_switch(xbox_state->buttons.b);
    }
    if (s_config.button_x_channel < CRSF_NUM_CHANNELS) {
        crsf_out->ch[s_config.button_x_channel] = crsf_scale_switch(xbox_state->buttons.x);
    }
    if (s_config.button_y_channel < CRSF_NUM_CHANNELS) {
        crsf_out->ch[s_config.button_y_channel] = crsf_scale_switch(xbox_state->buttons.y);
    }
    
    // D-pad could be useful for trim or mode selection
    // Map to a 3-position switch style if needed
    // For now, not mapped by default
}
