/**
 * UDP Log Broadcaster Implementation
 */

#include <string.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "esp_log.h"

#include "udp_log.h"

static const char *TAG = "udp_log";

static int s_socket = -1;
static struct sockaddr_in s_dest_addr;
static SemaphoreHandle_t s_mutex;
static vprintf_like_t s_original_vprintf;

// Buffer for formatting log messages
#define LOG_BUF_SIZE 512
static char s_log_buf[LOG_BUF_SIZE];

/**
 * Custom vprintf that sends to both UDP and original destination
 */
static int udp_log_vprintf(const char *fmt, va_list args)
{
    int ret = 0;
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        ret = vsnprintf(s_log_buf, LOG_BUF_SIZE, fmt, args);
        
        // Send via UDP (fire and forget)
        if (s_socket >= 0 && ret > 0) {
            sendto(s_socket, s_log_buf, ret, 0,
                   (struct sockaddr *)&s_dest_addr, sizeof(s_dest_addr));
        }
        
        // Write to stdout directly (UART if configured)
        if (ret > 0) {
            fwrite(s_log_buf, 1, ret, stdout);
        }
        
        xSemaphoreGive(s_mutex);
    }
    
    return ret;
}

esp_err_t udp_log_init(const char *host, uint16_t port)
{
    // Create mutex
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    // Create UDP socket
    s_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket: %d", errno);
        return ESP_FAIL;
    }
    
    // Set up destination address
    memset(&s_dest_addr, 0, sizeof(s_dest_addr));
    s_dest_addr.sin_family = AF_INET;
    s_dest_addr.sin_port = htons(port);
    
    if (host == NULL) {
        // Broadcast
        s_dest_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        
        // Enable broadcast on socket
        int broadcast = 1;
        setsockopt(s_socket, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
        
        ESP_LOGI(TAG, "UDP logging to broadcast:%d", port);
    } else {
        // Specific host
        if (inet_pton(AF_INET, host, &s_dest_addr.sin_addr) != 1) {
            ESP_LOGE(TAG, "Invalid IP address: %s", host);
            close(s_socket);
            s_socket = -1;
            return ESP_ERR_INVALID_ARG;
        }
        ESP_LOGI(TAG, "UDP logging to %s:%d", host, port);
    }
    
    // Hook into esp_log
    s_original_vprintf = esp_log_set_vprintf(udp_log_vprintf);
    
    ESP_LOGI(TAG, "UDP logging initialized");
    
    return ESP_OK;
}

void udp_log_deinit(void)
{
    // Restore original vprintf
    if (s_original_vprintf) {
        esp_log_set_vprintf(s_original_vprintf);
        s_original_vprintf = NULL;
    }
    
    // Close socket
    if (s_socket >= 0) {
        close(s_socket);
        s_socket = -1;
    }
    
    // Delete mutex
    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
}
