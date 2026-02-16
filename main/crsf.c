/**
 * CRSF Protocol Implementation
 * 
 * Sends RC channel data to ELRS TX module via UART.
 * 
 * The channel data frame packs 16 channels of 11-bit data into 22 bytes,
 * plus sync, length, type, and CRC = 26 bytes total.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "esp_log.h"

#include "crsf.h"

static const char *TAG = "crsf";

// CRSF state
static int s_uart_num = -1;
static crsf_channels_t s_channels;
static SemaphoreHandle_t s_channels_mutex;
static bool s_running = false;
static TaskHandle_t s_task_handle = NULL;
static uint32_t s_interval_ms = 4;

// Failsafe channel values (sent when controller disconnects)
static crsf_channels_t s_failsafe_channels;

// CRC8 lookup table (polynomial 0xD5)
static const uint8_t crc8_lut[256] = {
    0x00, 0xD5, 0x7F, 0xAA, 0xFE, 0x2B, 0x81, 0x54,
    0x29, 0xFC, 0x56, 0x83, 0xD7, 0x02, 0xA8, 0x7D,
    0x52, 0x87, 0x2D, 0xF8, 0xAC, 0x79, 0xD3, 0x06,
    0x7B, 0xAE, 0x04, 0xD1, 0x85, 0x50, 0xFA, 0x2F,
    0xA4, 0x71, 0xDB, 0x0E, 0x5A, 0x8F, 0x25, 0xF0,
    0x8D, 0x58, 0xF2, 0x27, 0x73, 0xA6, 0x0C, 0xD9,
    0xF6, 0x23, 0x89, 0x5C, 0x08, 0xDD, 0x77, 0xA2,
    0xDF, 0x0A, 0xA0, 0x75, 0x21, 0xF4, 0x5E, 0x8B,
    0x9D, 0x48, 0xE2, 0x37, 0x63, 0xB6, 0x1C, 0xC9,
    0xB4, 0x61, 0xCB, 0x1E, 0x4A, 0x9F, 0x35, 0xE0,
    0xCF, 0x1A, 0xB0, 0x65, 0x31, 0xE4, 0x4E, 0x9B,
    0xE6, 0x33, 0x99, 0x4C, 0x18, 0xCD, 0x67, 0xB2,
    0x39, 0xEC, 0x46, 0x93, 0xC7, 0x12, 0xB8, 0x6D,
    0x10, 0xC5, 0x6F, 0xBA, 0xEE, 0x3B, 0x91, 0x44,
    0x6B, 0xBE, 0x14, 0xC1, 0x95, 0x40, 0xEA, 0x3F,
    0x42, 0x97, 0x3D, 0xE8, 0xBC, 0x69, 0xC3, 0x16,
    0xEF, 0x3A, 0x90, 0x45, 0x11, 0xC4, 0x6E, 0xBB,
    0xC6, 0x13, 0xB9, 0x6C, 0x38, 0xED, 0x47, 0x92,
    0xBD, 0x68, 0xC2, 0x17, 0x43, 0x96, 0x3C, 0xE9,
    0x94, 0x41, 0xEB, 0x3E, 0x6A, 0xBF, 0x15, 0xC0,
    0x4B, 0x9E, 0x34, 0xE1, 0xB5, 0x60, 0xCA, 0x1F,
    0x62, 0xB7, 0x1D, 0xC8, 0x9C, 0x49, 0xE3, 0x36,
    0x19, 0xCC, 0x66, 0xB3, 0xE7, 0x32, 0x98, 0x4D,
    0x30, 0xE5, 0x4F, 0x9A, 0xCE, 0x1B, 0xB1, 0x64,
    0x72, 0xA7, 0x0D, 0xD8, 0x8C, 0x59, 0xF3, 0x26,
    0x5B, 0x8E, 0x24, 0xF1, 0xA5, 0x70, 0xDA, 0x0F,
    0x20, 0xF5, 0x5F, 0x8A, 0xDE, 0x0B, 0xA1, 0x74,
    0x09, 0xDC, 0x76, 0xA3, 0xF7, 0x22, 0x88, 0x5D,
    0xD6, 0x03, 0xA9, 0x7C, 0x28, 0xFD, 0x57, 0x82,
    0xFF, 0x2A, 0x80, 0x55, 0x01, 0xD4, 0x7E, 0xAB,
    0x84, 0x51, 0xFB, 0x2E, 0x7A, 0xAF, 0x05, 0xD0,
    0xAD, 0x78, 0xD2, 0x07, 0x53, 0x86, 0x2C, 0xF9,
};

/**
 * Calculate CRC8 over buffer
 */
static uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc = crc8_lut[crc ^ data[i]];
    }
    return crc;
}

/**
 * Pack 16 channels (11-bit each) into 22 bytes
 * 
 * Bit packing: channels are packed LSB first
 *   ch0[0:7]   -> byte 0
 *   ch0[8:10], ch1[0:4] -> byte 1
 *   ch1[5:10], ch2[0:1] -> byte 2
 *   ... etc
 */
static void pack_channels(const crsf_channels_t *channels, uint8_t *packed)
{
    // This is a bit-packed format, 11 bits per channel
    // Using a simple approach: pack into a bit buffer then extract bytes
    
    packed[0]  = (uint8_t)(channels->ch[0] & 0xFF);
    packed[1]  = (uint8_t)((channels->ch[0] >> 8) | ((channels->ch[1] & 0x1F) << 3));
    packed[2]  = (uint8_t)((channels->ch[1] >> 5) | ((channels->ch[2] & 0x03) << 6));
    packed[3]  = (uint8_t)((channels->ch[2] >> 2) & 0xFF);
    packed[4]  = (uint8_t)((channels->ch[2] >> 10) | ((channels->ch[3] & 0x7F) << 1));
    packed[5]  = (uint8_t)((channels->ch[3] >> 7) | ((channels->ch[4] & 0x0F) << 4));
    packed[6]  = (uint8_t)((channels->ch[4] >> 4) | ((channels->ch[5] & 0x01) << 7));
    packed[7]  = (uint8_t)((channels->ch[5] >> 1) & 0xFF);
    packed[8]  = (uint8_t)((channels->ch[5] >> 9) | ((channels->ch[6] & 0x3F) << 2));
    packed[9]  = (uint8_t)((channels->ch[6] >> 6) | ((channels->ch[7] & 0x07) << 5));
    packed[10] = (uint8_t)((channels->ch[7] >> 3) & 0xFF);
    packed[11] = (uint8_t)(channels->ch[8] & 0xFF);
    packed[12] = (uint8_t)((channels->ch[8] >> 8) | ((channels->ch[9] & 0x1F) << 3));
    packed[13] = (uint8_t)((channels->ch[9] >> 5) | ((channels->ch[10] & 0x03) << 6));
    packed[14] = (uint8_t)((channels->ch[10] >> 2) & 0xFF);
    packed[15] = (uint8_t)((channels->ch[10] >> 10) | ((channels->ch[11] & 0x7F) << 1));
    packed[16] = (uint8_t)((channels->ch[11] >> 7) | ((channels->ch[12] & 0x0F) << 4));
    packed[17] = (uint8_t)((channels->ch[12] >> 4) | ((channels->ch[13] & 0x01) << 7));
    packed[18] = (uint8_t)((channels->ch[13] >> 1) & 0xFF);
    packed[19] = (uint8_t)((channels->ch[13] >> 9) | ((channels->ch[14] & 0x3F) << 2));
    packed[20] = (uint8_t)((channels->ch[14] >> 6) | ((channels->ch[15] & 0x07) << 5));
    packed[21] = (uint8_t)((channels->ch[15] >> 3) & 0xFF);
}

/**
 * Build and send a CRSF RC channels frame
 */
static void send_channels_frame(void)
{
    // Frame format:
    //   [0] Sync byte (0xC8)
    //   [1] Frame length (24 = type + payload + crc)
    //   [2] Frame type (0x16 = RC_CHANNELS_PACKED)
    //   [3-24] Payload (22 bytes of packed channel data)
    //   [25] CRC8 (over bytes 2-24)
    
    uint8_t frame[26];
    
    frame[0] = CRSF_SYNC_BYTE;
    frame[1] = 24;  // length: type(1) + payload(22) + crc(1)
    frame[2] = CRSF_FRAMETYPE_RC_CHANNELS_PACKED;
    
    // Get current channel data
    crsf_channels_t channels;
    if (xSemaphoreTake(s_channels_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        memcpy(&channels, &s_channels, sizeof(crsf_channels_t));
        xSemaphoreGive(s_channels_mutex);
    } else {
        // Mutex timeout - use safe defaults
        for (int i = 0; i < CRSF_NUM_CHANNELS; i++) {
            channels.ch[i] = CRSF_CHANNEL_MID;
        }
    }
    
    // Pack channel data
    pack_channels(&channels, &frame[3]);
    
    // Calculate CRC over type + payload
    frame[25] = crc8(&frame[2], 23);
    
    // Send frame
    uart_write_bytes(s_uart_num, frame, sizeof(frame));
}

/**
 * Periodic task that sends CRSF channel frames
 */
static void crsf_task(void *pvParameters)
{
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        if (s_running) {
            send_channels_frame();
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(s_interval_ms));
    }
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t crsf_init(const crsf_config_t *config)
{
    if (config == NULL || config->uart_num < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_uart_num = config->uart_num;
    
    // Create mutex
    s_channels_mutex = xSemaphoreCreateMutex();
    if (s_channels_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize channels to center/safe values
    for (int i = 0; i < CRSF_NUM_CHANNELS; i++) {
        s_channels.ch[i] = CRSF_CHANNEL_MID;
        s_failsafe_channels.ch[i] = CRSF_CHANNEL_MID;
    }
    // Default failsafe: throttle at MIN (stopped)
    s_failsafe_channels.ch[2] = CRSF_CHANNEL_MIN;
    
    // Configure UART
    uart_config_t uart_config = {
        .baud_rate = CRSF_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    esp_err_t err = uart_param_config(s_uart_num, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return err;
    }
    
    // Set pins
    int rx_pin = config->rx_pin >= 0 ? config->rx_pin : UART_PIN_NO_CHANGE;
    err = uart_set_pin(s_uart_num, config->tx_pin, rx_pin, 
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return err;
    }
    
    // Install UART driver
    err = uart_driver_install(s_uart_num, 256, 256, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "CRSF UART initialized: %d baud on GPIO%d",
             CRSF_BAUDRATE, config->tx_pin);

    // Start periodic send task
    s_interval_ms = config->interval_ms > 0 ? config->interval_ms : 4;
    s_running = true;
    BaseType_t ret = xTaskCreate(crsf_task, "crsf_send", 2048, NULL, 10, &s_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create CRSF task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void crsf_set_channels(const crsf_channels_t *channels)
{
    if (channels == NULL) return;

    if (xSemaphoreTake(s_channels_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(&s_channels, channels, sizeof(crsf_channels_t));
        xSemaphoreGive(s_channels_mutex);
    }
}

void crsf_set_channel(uint8_t channel, uint16_t value)
{
    if (channel >= CRSF_NUM_CHANNELS) return;
    
    // Clamp value to valid range
    if (value < CRSF_CHANNEL_MIN) value = CRSF_CHANNEL_MIN;
    if (value > CRSF_CHANNEL_MAX) value = CRSF_CHANNEL_MAX;
    
    if (xSemaphoreTake(s_channels_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s_channels.ch[channel] = value;
        xSemaphoreGive(s_channels_mutex);
    }
}

void crsf_get_channels(crsf_channels_t *channels)
{
    if (channels == NULL) return;
    
    if (xSemaphoreTake(s_channels_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(channels, &s_channels, sizeof(crsf_channels_t));
        xSemaphoreGive(s_channels_mutex);
    }
}

esp_err_t crsf_start(void)
{
    if (s_running) {
        return ESP_OK;  // Already running
    }
    
    s_running = true;
    ESP_LOGI(TAG, "CRSF transmission started");
    
    return ESP_OK;
}

void crsf_stop(void)
{
    s_running = false;
    ESP_LOGI(TAG, "CRSF transmission stopped");
}

void crsf_set_failsafe(const crsf_channels_t *channels)
{
    if (channels == NULL) return;

    if (xSemaphoreTake(s_channels_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(&s_failsafe_channels, channels, sizeof(crsf_channels_t));
        xSemaphoreGive(s_channels_mutex);
    }
}

