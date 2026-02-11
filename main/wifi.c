/**
 * WiFi Station Mode Implementation
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mdns.h"

#include "wifi.h"

static const char *TAG = "wifi";

// Event group bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static bool s_connected = false;
static esp_ip4_addr_t s_ip_addr;
static int s_retry_count = 0;
static bool s_ever_connected = false;

#define MAX_RETRY 10

static void mdns_init_service(void)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(err));
        return;
    }
    
    mdns_hostname_set("xbox-elrs");
    mdns_instance_name_set("Xbox ELRS Bridge");
    
    // Advertise our UDP services
    mdns_service_add(NULL, "_xbox-elrs-log", "_udp", 3333, NULL, 0);
    mdns_service_add(NULL, "_xbox-elrs-ota", "_udp", 3334, NULL, 0);
    
    ESP_LOGI(TAG, "mDNS: xbox-elrs.local");
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_ever_connected || s_retry_count < MAX_RETRY) {
            // Always retry after a successful connection (survives router reboots).
            // Initial connection has a retry limit so wifi_init_sta can fail fast.
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGI(TAG, "Retrying WiFi connection...");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Connection failed after %d attempts", MAX_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_ip_addr = event->ip_info.ip;
        ESP_LOGI(TAG, "Connected, IP: " IPSTR, IP2STR(&s_ip_addr));
        s_retry_count = 0;
        s_connected = true;
        s_ever_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        mdns_init_service();
    }
}

esp_err_t wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    // Note: event loop should already be created by main
    
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to '%s'...", CONFIG_WIFI_SSID);

    // Wait for connection or failure
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(30000)  // 30 second timeout
    );

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "Connection timeout");
        return ESP_ERR_TIMEOUT;
    }
}

bool wifi_is_connected(void)
{
    return s_connected;
}

void wifi_get_ip_str(char *buf, size_t len)
{
    if (s_connected) {
        snprintf(buf, len, IPSTR, IP2STR(&s_ip_addr));
    } else {
        snprintf(buf, len, "0.0.0.0");
    }
}
