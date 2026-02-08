/**
 * UDP Log Broadcaster
 * 
 * Redirects ESP_LOG output to UDP for wireless monitoring.
 * Packets are fire-and-forget - no connection state, no ACKs.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize UDP logging
 * 
 * Call after WiFi is connected. Redirects esp_log output to UDP
 * while still printing to UART (if available).
 * 
 * @param host Target IP address (e.g., "192.168.1.100") or NULL for broadcast
 * @param port Target UDP port
 * @return ESP_OK on success
 */
esp_err_t udp_log_init(const char *host, uint16_t port);

/**
 * Stop UDP logging and restore default output
 */
void udp_log_deinit(void);

#ifdef __cplusplus
}
#endif
