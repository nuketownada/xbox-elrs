/**
 * CRSF (Crossfire) Protocol Implementation
 * 
 * CRSF is the serial protocol used by TBS Crossfire and ExpressLRS (ELRS).
 * This implements the TX side (handset → TX module).
 * 
 * Protocol spec: https://github.com/crsf-wg/crsf/wiki
 * 
 * Frame format:
 *   [sync] [len] [type] [payload...] [crc8]
 *   
 * Channel data uses 11-bit resolution packed into CRSF_FRAMETYPE_RC_CHANNELS_PACKED
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// CRSF protocol constants
#define CRSF_SYNC_BYTE           0xC8
#define CRSF_BAUDRATE            420000

// CRSF channel value range (11-bit)
#define CRSF_CHANNEL_MIN         172    // 988us equivalent
#define CRSF_CHANNEL_MID         992    // 1500us equivalent  
#define CRSF_CHANNEL_MAX         1811   // 2012us equivalent

// Number of RC channels in standard frame
#define CRSF_NUM_CHANNELS        16

// Frame types we care about
#define CRSF_FRAMETYPE_RC_CHANNELS_PACKED  0x16
#define CRSF_FRAMETYPE_LINK_STATISTICS     0x14

// CRSF channel data (16 channels, 11-bit each)
typedef struct {
    uint16_t ch[CRSF_NUM_CHANNELS];  // Values: CRSF_CHANNEL_MIN to CRSF_CHANNEL_MAX
} crsf_channels_t;

// Configuration for CRSF output
typedef struct {
    int uart_num;       // UART peripheral (UART_NUM_1 recommended)
    int tx_pin;         // GPIO for TX
    int rx_pin;         // GPIO for RX (optional, -1 to disable)
    uint32_t interval_ms;  // Packet interval (default 4ms for ELRS 250Hz)
} crsf_config_t;

/**
 * Initialize CRSF UART output
 * 
 * @param config UART and timing configuration
 * @return ESP_OK on success
 */
esp_err_t crsf_init(const crsf_config_t *config);

/**
 * Update channel values
 * 
 * Channel values should be in CRSF range (172-1811).
 * Use crsf_scale_* helpers to convert from other formats.
 * 
 * @param channels Pointer to channel data
 */
void crsf_set_channels(const crsf_channels_t *channels);

/**
 * Set a single channel value
 * 
 * @param channel Channel index (0-15)
 * @param value CRSF value (172-1811)
 */
void crsf_set_channel(uint8_t channel, uint16_t value);

/**
 * Get current channel values
 */
void crsf_get_channels(crsf_channels_t *channels);

/**
 * Start CRSF transmission task
 * 
 * Spawns a FreeRTOS task that sends channel packets at configured interval.
 */
esp_err_t crsf_start(void);

/**
 * Stop CRSF transmission
 */
void crsf_stop(void);

// ============================================================================
// Scaling helpers
// ============================================================================

/**
 * Scale signed 16-bit axis (-32768 to 32767) to CRSF range
 */
static inline uint16_t crsf_scale_axis(int16_t value) {
    // Map -32768..32767 to 172..1811
    int32_t scaled = ((int32_t)value + 32768) * (CRSF_CHANNEL_MAX - CRSF_CHANNEL_MIN);
    scaled /= 65535;
    return (uint16_t)(scaled + CRSF_CHANNEL_MIN);
}

/**
 * Scale unsigned 8-bit value (0-255) to CRSF range
 */
static inline uint16_t crsf_scale_trigger(uint8_t value) {
    // Map 0..255 to 172..1811
    uint32_t scaled = (uint32_t)value * (CRSF_CHANNEL_MAX - CRSF_CHANNEL_MIN);
    scaled /= 255;
    return (uint16_t)(scaled + CRSF_CHANNEL_MIN);
}

/**
 * Scale boolean to CRSF (false = min, true = max)
 */
static inline uint16_t crsf_scale_switch(bool value) {
    return value ? CRSF_CHANNEL_MAX : CRSF_CHANNEL_MIN;
}

/**
 * Scale boolean to CRSF 3-position (-1, 0, 1)
 */
static inline uint16_t crsf_scale_3pos(int8_t position) {
    if (position < 0) return CRSF_CHANNEL_MIN;
    if (position > 0) return CRSF_CHANNEL_MAX;
    return CRSF_CHANNEL_MID;
}

#ifdef __cplusplus
}
#endif
