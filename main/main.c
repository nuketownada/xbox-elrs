/**
 * Xbox 360 Racing Wheel to ELRS Transmitter Bridge
 * 
 * Reads input from an Xbox 360 wireless racing wheel via Microsoft's
 * wireless receiver dongle, processes the inputs through a configurable
 * mixer, and outputs CRSF protocol to an ELRS TX module.
 * 
 * Hardware:
 *   - Seeed XIAO ESP32-S3 (or similar S3 board)
 *   - Microsoft Xbox 360 Wireless Receiver for Windows
 *   - ELRS TX module (any, running TX firmware)
 *   - Xbox 360 wireless racing wheel with force feedback
 * 
 * Connections:
 *   XIAO USB-C     → Computer (programming/debug console)
 *   XIAO D+/D- pads → USB-A female connector → Xbox receiver
 *   XIAO GPIO43     → ELRS TX CRSF input (typically labeled "S" or "SBUS")
 *   XIAO 5V/GND    → Both Xbox receiver and ELRS TX power
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_event.h"

#include "xbox_receiver.h"
#include "crsf.h"
#include "channel_mixer.h"
#include "wifi.h"
#include "udp_log.h"
#include "ota.h"

static const char *TAG = "xbox-elrs";

// CRSF output configuration
// Using GPIO43 (D6 on XIAO) for CRSF TX
#define CRSF_TX_PIN  43
#define CRSF_RX_PIN  -1  // Not used (TX only)

// Network ports
#define UDP_LOG_PORT  3333
#define OTA_CMD_PORT  3334

// Current channel state (updated by callback, read by CRSF task)
static crsf_channels_t g_channels;
static SemaphoreHandle_t g_channels_mutex;

// Mixer configuration
static mixer_config_t g_mixer_config = MIXER_CONFIG_DEFAULT();

/**
 * Callback from Xbox receiver when controller state changes
 */
static void xbox_state_callback(xbox_slot_t slot, const xbox_controller_state_t *state)
{
    // Only process slot 0 (first controller / wheel)
    if (slot != XBOX_SLOT_1) {
        return;
    }
    
    if (!state->connected) {
        ESP_LOGW(TAG, "Racing wheel disconnected");
        // Set channels to safe defaults (center steering, no throttle)
        if (xSemaphoreTake(g_channels_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            for (int i = 0; i < CRSF_NUM_CHANNELS; i++) {
                g_channels.ch[i] = CRSF_CHANNEL_MID;
            }
            g_channels.ch[RC_CH_THROTTLE] = CRSF_CHANNEL_MIN;  // Throttle to minimum
            xSemaphoreGive(g_channels_mutex);
        }
        return;
    }
    
    // Process through mixer
    crsf_channels_t new_channels;
    mixer_process(state, &new_channels);
    
    // Update global channel state
    if (xSemaphoreTake(g_channels_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(&g_channels, &new_channels, sizeof(crsf_channels_t));
        xSemaphoreGive(g_channels_mutex);
    }
    
    // Debug output - log on change
    static int16_t last_steer = 0;
    static uint8_t last_throttle = 0;
    static uint8_t last_brake = 0;
    if (state->left_stick_x != last_steer || 
        state->right_trigger != last_throttle || 
        state->left_trigger != last_brake) {
        last_steer = state->left_stick_x;
        last_throttle = state->right_trigger;
        last_brake = state->left_trigger;
        ESP_LOGI(TAG, "Steer: %6d  Throttle: %3d  Brake: %3d",
            state->left_stick_x,
            state->right_trigger,
            state->left_trigger);
    }
}

/**
 * Task that periodically sends CRSF packets
 */
static void crsf_send_task(void *pvParameters)
{
    const TickType_t interval = pdMS_TO_TICKS(4);  // 250Hz for ELRS
    TickType_t last_wake = xTaskGetTickCount();
    
    while (1) {
        // Get current channel state
        if (xSemaphoreTake(g_channels_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            crsf_set_channels(&g_channels);
            xSemaphoreGive(g_channels_mutex);
        }
        
        // Wait for next interval
        vTaskDelayUntil(&last_wake, interval);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Xbox 360 Racing Wheel to ELRS Bridge starting...");
    
    // Initialize NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create mutex for channel data
    g_channels_mutex = xSemaphoreCreateMutex();
    if (g_channels_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }
    
    // Initialize channels to safe defaults
    for (int i = 0; i < CRSF_NUM_CHANNELS; i++) {
        g_channels.ch[i] = CRSF_CHANNEL_MID;
    }
    g_channels.ch[RC_CH_THROTTLE] = CRSF_CHANNEL_MIN;
    
    // Connect to WiFi (for logging and OTA)
    ESP_LOGI(TAG, "Connecting to WiFi...");
    ret = wifi_init_sta();
    if (ret == ESP_OK) {
        char ip[16];
        wifi_get_ip_str(ip, sizeof(ip));
        ESP_LOGI(TAG, "WiFi connected: %s", ip);
        
        // Start UDP logging (broadcast to network)
        udp_log_init(NULL, UDP_LOG_PORT);
        ESP_LOGI(TAG, "UDP logging on port %d (broadcast)", UDP_LOG_PORT);
        
        // Start OTA command server
        ota_server_start(OTA_CMD_PORT);
        ESP_LOGI(TAG, "OTA server on port %d", OTA_CMD_PORT);
    } else {
        ESP_LOGW(TAG, "WiFi connection failed - continuing without network features");
    }
    
    // Initialize mixer
    ESP_ERROR_CHECK(mixer_init(&g_mixer_config));
    ESP_LOGI(TAG, "Mixer initialized");
    
    // Initialize CRSF output
    crsf_config_t crsf_config = {
        .uart_num = 1,  // UART1
        .tx_pin = CRSF_TX_PIN,
        .rx_pin = CRSF_RX_PIN,
        .interval_ms = 4,
    };
    ESP_ERROR_CHECK(crsf_init(&crsf_config));
    ESP_LOGI(TAG, "CRSF initialized on GPIO%d", CRSF_TX_PIN);
    
    // Start CRSF transmission task
    xTaskCreate(crsf_send_task, "crsf_send", 2048, NULL, 10, NULL);
    
    // Initialize Xbox receiver (this blocks until receiver is connected)
    ESP_LOGI(TAG, "Initializing USB host for Xbox receiver...");
    ESP_ERROR_CHECK(xbox_receiver_init(xbox_state_callback));
    ESP_LOGI(TAG, "Xbox receiver initialized");
    
    // Main loop - just status reporting
    while (1) {
        if (xbox_receiver_is_connected()) {
            xbox_controller_state_t state;
            if (xbox_receiver_get_state(XBOX_SLOT_1, &state) == ESP_OK) {
                // Wheel is connected and sending data
            }
        } else {
            ESP_LOGW(TAG, "Waiting for Xbox receiver...");
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
