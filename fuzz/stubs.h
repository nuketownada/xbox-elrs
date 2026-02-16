/**
 * ESP-IDF / FreeRTOS stubs for host-side fuzz and test builds.
 *
 * This header shadows the real ESP-IDF includes so that the firmware
 * sources (crsf.c, channel_mixer.c, xbox_receiver.c) compile on a
 * regular Linux host with clang.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* esp_err.h                                                          */
/* ------------------------------------------------------------------ */

typedef int esp_err_t;
#define ESP_OK              0
#define ESP_ERR_INVALID_ARG (-1)
#define ESP_ERR_NO_MEM      (-2)
#define ESP_ERR_NOT_FOUND   (-3)
#define ESP_ERR_TIMEOUT     (-4)
#define ESP_ERR_NOT_SUPPORTED (-5)

static inline const char *esp_err_to_name(esp_err_t err) {
    (void)err;
    return "stub";
}

#define ESP_ERROR_CHECK(x) do { (void)(x); } while(0)

/* ------------------------------------------------------------------ */
/* FreeRTOS types                                                      */
/* ------------------------------------------------------------------ */

typedef uint32_t TickType_t;
typedef int      BaseType_t;
typedef void*    TaskHandle_t;
typedef void*    SemaphoreHandle_t;

#define pdTRUE    1
#define pdFALSE   0
#define pdPASS    1
#define portMAX_DELAY 0xFFFFFFFF

/* Controllable tick count for deterministic testing */
extern uint32_t g_tick_count;

static inline TickType_t xTaskGetTickCount(void) {
    return (TickType_t)g_tick_count;
}

static inline TickType_t pdMS_TO_TICKS(uint32_t ms) {
    /* 1 tick = 1 ms for simplicity */
    return (TickType_t)ms;
}

/* ------------------------------------------------------------------ */
/* Semaphore stubs                                                     */
/* ------------------------------------------------------------------ */

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    static int dummy;
    return (SemaphoreHandle_t)&dummy;
}

static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t t) {
    (void)s; (void)t;
    return pdTRUE;
}

static inline void xSemaphoreGive(SemaphoreHandle_t s) {
    (void)s;
}

static inline SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    return xSemaphoreCreateMutex();
}

/* ------------------------------------------------------------------ */
/* Task stubs                                                          */
/* ------------------------------------------------------------------ */

static inline BaseType_t xTaskCreate(void (*fn)(void*), const char *name,
                                     uint32_t stack, void *arg, int prio,
                                     TaskHandle_t *out) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)prio;
    if (out) *out = NULL;
    return pdPASS;
}

static inline void vTaskDelay(TickType_t ticks) { (void)ticks; }

static inline void vTaskDelayUntil(TickType_t *prev, TickType_t inc) {
    (void)prev; (void)inc;
}

static inline void vTaskDelete(TaskHandle_t t) { (void)t; }

/* ------------------------------------------------------------------ */
/* ESP log stubs                                                       */
/* ------------------------------------------------------------------ */

#define ESP_LOGE(tag, fmt, ...) (void)0
#define ESP_LOGW(tag, fmt, ...) (void)0
#define ESP_LOGI(tag, fmt, ...) (void)0
#define ESP_LOGD(tag, fmt, ...) (void)0

/* ------------------------------------------------------------------ */
/* UART stubs                                                          */
/* ------------------------------------------------------------------ */

/* Capture last UART write for inspection */
#define UART_BUF_SIZE 256
extern uint8_t g_uart_buf[UART_BUF_SIZE];
extern size_t  g_uart_len;

typedef struct {
    int baud_rate;
    int data_bits;
    int parity;
    int stop_bits;
    int flow_ctrl;
    int source_clk;
} uart_config_t;

#define UART_DATA_8_BITS    0
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1    0
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT   0
#define UART_PIN_NO_CHANGE  (-1)

static inline esp_err_t uart_param_config(int num, const uart_config_t *c) {
    (void)num; (void)c;
    return ESP_OK;
}

static inline esp_err_t uart_set_pin(int num, int tx, int rx, int rts, int cts) {
    (void)num; (void)tx; (void)rx; (void)rts; (void)cts;
    return ESP_OK;
}

static inline esp_err_t uart_driver_install(int num, int rx_buf, int tx_buf,
                                            int queue_sz, void *queue, int flags) {
    (void)num; (void)rx_buf; (void)tx_buf; (void)queue_sz; (void)queue; (void)flags;
    return ESP_OK;
}

static inline int uart_write_bytes(int num, const void *data, size_t len) {
    (void)num;
    if (len > UART_BUF_SIZE) len = UART_BUF_SIZE;
    memcpy(g_uart_buf, data, len);
    g_uart_len = len;
    return (int)len;
}

/* ------------------------------------------------------------------ */
/* USB Host stubs (types only, functions are no-ops)                    */
/* ------------------------------------------------------------------ */

typedef void* usb_host_client_handle_t;
typedef void* usb_device_handle_t;

/* Forward declare for callback type */
struct usb_transfer_s;
typedef struct usb_transfer_s usb_transfer_t;

struct usb_transfer_s {
    int status;
    int actual_num_bytes;
    int num_bytes;
    uint8_t bEndpointAddress;
    uint8_t *data_buffer;
    void (*callback)(usb_transfer_t *);
    void *context;
    void *device_handle;
};

typedef struct { uint16_t idVendor; uint16_t idProduct; } usb_device_desc_t;
typedef struct { uint16_t wTotalLength; } usb_config_desc_t;
typedef struct { uint8_t bLength; uint8_t bDescriptorType; } usb_standard_desc_t;
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
} usb_ep_desc_t;

typedef struct {
    int event;
    union { struct { uint8_t address; } new_dev; };
} usb_host_client_event_msg_t;

typedef struct { int skip_phy_setup; int intr_flags; } usb_host_config_t;
typedef struct {
    int is_synchronous;
    int max_num_event_msg;
    struct {
        void (*client_event_callback)(const usb_host_client_event_msg_t*, void*);
        void *callback_arg;
    } async;
} usb_host_client_config_t;

#define USB_HOST_CLIENT_EVENT_NEW_DEV 0
#define USB_HOST_CLIENT_EVENT_DEV_GONE 1
#define USB_TRANSFER_STATUS_COMPLETED 0
#define USB_TRANSFER_STATUS_NO_DEVICE 1
#define USB_TRANSFER_STATUS_CANCELED 2
#define USB_B_DESCRIPTOR_TYPE_ENDPOINT 5
#define ESP_INTR_FLAG_LEVEL1 0
#define USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS 1
#define USB_HOST_LIB_EVENT_FLAGS_ALL_FREE 2

static inline esp_err_t usb_host_install(const usb_host_config_t *c) { (void)c; return ESP_OK; }
static inline esp_err_t usb_host_client_register(const usb_host_client_config_t *c, usb_host_client_handle_t *h) { (void)c; (void)h; return ESP_OK; }
static inline esp_err_t usb_host_device_open(usb_host_client_handle_t c, uint8_t a, usb_device_handle_t *h) { (void)c; (void)a; (void)h; return ESP_OK; }
static inline esp_err_t usb_host_get_device_descriptor(usb_device_handle_t h, const usb_device_desc_t **d) { (void)h; (void)d; return ESP_OK; }
static inline esp_err_t usb_host_get_active_config_descriptor(usb_device_handle_t h, const usb_config_desc_t **d) { (void)h; (void)d; return ESP_OK; }
static inline esp_err_t usb_host_interface_claim(usb_host_client_handle_t c, usb_device_handle_t d, int i, int a) { (void)c; (void)d; (void)i; (void)a; return ESP_OK; }
static inline esp_err_t usb_host_interface_release(usb_host_client_handle_t c, usb_device_handle_t d, int i) { (void)c; (void)d; (void)i; return ESP_OK; }
static inline esp_err_t usb_host_device_close(usb_host_client_handle_t c, usb_device_handle_t d) { (void)c; (void)d; return ESP_OK; }
static inline esp_err_t usb_host_transfer_alloc(int sz, int f, usb_transfer_t **t) { (void)sz; (void)f; (void)t; return ESP_OK; }
static inline void usb_host_transfer_free(usb_transfer_t *t) { (void)t; }
static inline esp_err_t usb_host_transfer_submit(usb_transfer_t *t) { (void)t; return ESP_OK; }
static inline esp_err_t usb_host_endpoint_halt(usb_device_handle_t d, uint8_t e) { (void)d; (void)e; return ESP_OK; }
static inline esp_err_t usb_host_endpoint_flush(usb_device_handle_t d, uint8_t e) { (void)d; (void)e; return ESP_OK; }
static inline esp_err_t usb_host_lib_handle_events(TickType_t t, uint32_t *f) { (void)t; (void)f; return ESP_OK; }
static inline void usb_host_client_handle_events(usb_host_client_handle_t h, TickType_t t) { (void)h; (void)t; }
