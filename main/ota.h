/**
 * Push-based OTA Update
 * 
 * Device listens on TCP, client pushes firmware directly.
 * Protocol: [4-byte size LE] [firmware data] -> "OK" or "FAIL"
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start OTA server
 * 
 * Listens for incoming firmware on TCP port.
 */
esp_err_t ota_server_start(uint16_t listen_port);

/**
 * Check if OTA update is in progress
 */
bool ota_in_progress(void);

#ifdef __cplusplus
}
#endif
