/**
 * Push-based OTA Update
 * 
 * Device listens on TCP port, client sends firmware directly.
 * Protocol: [4-byte size LE] [firmware data]
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "lwip/sockets.h"

#include "ota.h"

static const char *TAG = "ota";

static bool s_ota_in_progress = false;
static TaskHandle_t s_server_task = NULL;
static uint16_t s_listen_port = 3334;

#define OTA_BUF_SIZE 4096

static void handle_ota_connection(int client_sock)
{
    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *update_partition = NULL;
    esp_err_t err;
    uint8_t *buf = NULL;
    uint32_t firmware_size = 0;
    uint32_t received = 0;
    int len;
    
    s_ota_in_progress = true;
    
    buf = malloc(OTA_BUF_SIZE);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        goto cleanup;
    }
    
    // Read 4-byte size header
    len = recv(client_sock, &firmware_size, 4, MSG_WAITALL);
    if (len != 4) {
        ESP_LOGE(TAG, "Failed to read size header");
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "OTA starting, firmware size: %lu bytes", firmware_size);
    
    // Get update partition
    update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition found");
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "Writing to partition: %s", update_partition->label);
    
    err = esp_ota_begin(update_partition, firmware_size, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        goto cleanup;
    }
    
    // Receive and write firmware
    int last_progress = -1;
    while (received < firmware_size) {
        size_t to_read = firmware_size - received;
        if (to_read > OTA_BUF_SIZE) to_read = OTA_BUF_SIZE;
        
        len = recv(client_sock, buf, to_read, 0);
        if (len <= 0) {
            ESP_LOGE(TAG, "recv failed at %lu/%lu", received, firmware_size);
            goto cleanup;
        }
        
        err = esp_ota_write(ota_handle, buf, len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            goto cleanup;
        }
        
        received += len;
        
        int progress = (received * 100) / firmware_size;
        if (progress / 10 != last_progress / 10) {
            ESP_LOGI(TAG, "Progress: %d%% (%lu / %lu)", progress, received, firmware_size);
            last_progress = progress;
        }
    }
    
    ESP_LOGI(TAG, "Receive complete, verifying...");
    
    err = esp_ota_end(ota_handle);
    ota_handle = 0;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        goto cleanup;
    }
    
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        goto cleanup;
    }
    
    // Send success response
    const char *ok = "OK";
    send(client_sock, ok, 2, 0);
    
    ESP_LOGI(TAG, "OTA success! Rebooting...");
    free(buf);
    close(client_sock);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return;

cleanup:
    if (ota_handle) {
        esp_ota_abort(ota_handle);
    }
    if (buf) {
        free(buf);
    }
    const char *fail = "FAIL";
    send(client_sock, fail, 4, 0);
    close(client_sock);
    s_ota_in_progress = false;
}

static void ota_server_task(void *pvParameters)
{
    int server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        vTaskDelete(NULL);
        return;
    }
    
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(s_listen_port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Bind failed");
        close(server_sock);
        vTaskDelete(NULL);
        return;
    }
    
    if (listen(server_sock, 1) < 0) {
        ESP_LOGE(TAG, "Listen failed");
        close(server_sock);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "OTA server listening on TCP port %d", s_listen_port);
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
        if (client_sock < 0) {
            ESP_LOGE(TAG, "Accept failed");
            continue;
        }
        
        ESP_LOGI(TAG, "OTA connection from %s", inet_ntoa(client_addr.sin_addr));
        handle_ota_connection(client_sock);
    }
}

esp_err_t ota_server_start(uint16_t listen_port)
{
    if (s_server_task) {
        return ESP_OK;
    }
    
    s_listen_port = listen_port;
    
    BaseType_t ret = xTaskCreate(
        ota_server_task,
        "ota_server",
        8192,
        NULL,
        5,
        &s_server_task
    );
    
    return (ret == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

bool ota_in_progress(void)
{
    return s_ota_in_progress;
}
