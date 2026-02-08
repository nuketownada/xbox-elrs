/**
 * Xbox 360 Wireless Receiver USB Host Driver
 * 
 * Handles USB communication with Microsoft Xbox 360 Wireless Receiver.
 * This is a vendor-specific device (not standard HID), so we handle
 * enumeration and transfers at a low level.
 * 
 * Protocol reference: Linux xpad driver
 * https://github.com/torvalds/linux/blob/master/drivers/input/joystick/xpad.c
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Xbox 360 Wireless Receiver USB IDs
#define XBOX_RECEIVER_VID  0x045E  // Microsoft
#define XBOX_RECEIVER_PID  0x0719  // Xbox 360 Wireless Receiver

// Controller slot (receiver supports up to 4 controllers)
typedef enum {
    XBOX_SLOT_1 = 0,
    XBOX_SLOT_2,
    XBOX_SLOT_3,
    XBOX_SLOT_4,
    XBOX_SLOT_MAX
} xbox_slot_t;

// Digital button flags (directly map to protocol bits)
typedef struct {
    bool dpad_up;
    bool dpad_down;
    bool dpad_left;
    bool dpad_right;
    bool start;
    bool back;
    bool left_stick;   // L3
    bool right_stick;  // R3
    bool lb;           // Left bumper
    bool rb;           // Right bumper
    bool guide;        // Xbox button
    bool a;
    bool b;
    bool x;
    bool y;
} xbox_buttons_t;

// Full controller state
typedef struct {
    bool connected;
    
    // Analog axes (raw 16-bit values)
    int16_t left_stick_x;   // -32768 to 32767
    int16_t left_stick_y;
    int16_t right_stick_x;
    int16_t right_stick_y;
    
    // Triggers (0-255)
    uint8_t left_trigger;
    uint8_t right_trigger;
    
    // Digital buttons
    xbox_buttons_t buttons;
    
    // For racing wheel specifically:
    // - Steering maps to left_stick_x
    // - Throttle maps to right_trigger  
    // - Brake maps to left_trigger
    // - Paddle shifters map to A/B or bumpers (varies by wheel)
} xbox_controller_state_t;

// Callback for controller state updates
typedef void (*xbox_state_callback_t)(xbox_slot_t slot, const xbox_controller_state_t *state);

/**
 * Initialize Xbox 360 wireless receiver USB host driver
 * 
 * @param callback Function to call when controller state changes
 * @return ESP_OK on success
 */
esp_err_t xbox_receiver_init(xbox_state_callback_t callback);

/**
 * Get current state for a controller slot
 * 
 * @param slot Controller slot (0-3)
 * @param state Output state structure
 * @return ESP_OK if controller connected, ESP_ERR_NOT_FOUND otherwise
 */
esp_err_t xbox_receiver_get_state(xbox_slot_t slot, xbox_controller_state_t *state);

/**
 * Set rumble motors (if supported by controller)
 * 
 * @param slot Controller slot
 * @param left_motor Left motor intensity (0-255)
 * @param right_motor Right motor intensity (0-255)
 * @return ESP_OK on success
 */
esp_err_t xbox_receiver_set_rumble(xbox_slot_t slot, uint8_t left_motor, uint8_t right_motor);

/**
 * Check if USB receiver is connected and enumerated
 */
bool xbox_receiver_is_connected(void);

#ifdef __cplusplus
}
#endif
