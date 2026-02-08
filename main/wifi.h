/**
 * WiFi Station Mode
 * 
 * Simple WiFi connection for UDP logging and OTA updates.
 * Credentials configured via menuconfig or sdkconfig.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize WiFi in station mode and connect
 * 
 * Blocks until connected or timeout.
 * Credentials from CONFIG_WIFI_SSID and CONFIG_WIFI_PASSWORD.
 * 
 * @return ESP_OK on successful connection
 */
esp_err_t wifi_init_sta(void);

/**
 * Check if WiFi is connected
 */
bool wifi_is_connected(void);

/**
 * Get current IP address as string
 * 
 * @param buf Output buffer (at least 16 bytes)
 * @param len Buffer length
 */
void wifi_get_ip_str(char *buf, size_t len);

#ifdef __cplusplus
}
#endif
