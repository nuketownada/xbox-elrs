/**
 * Xbox 360 Wireless Receiver USB Host Driver
 * 
 * Implementation notes:
 * 
 * The Xbox 360 wireless receiver is a vendor-specific USB device.
 * It presents multiple interfaces (one per controller slot).
 * Each interface has IN and OUT interrupt endpoints.
 * 
 * Protocol is documented via reverse engineering:
 * - Linux xpad driver: drivers/input/joystick/xpad.c
 * - Various Arduino libraries
 * 
 * Input report format (20 bytes for wireless):
 *   [0]     Report type (0x00 = controller data)
 *   [1]     Report length
 *   [2-3]   Button bitfield
 *   [4]     Left trigger (0-255)
 *   [5]     Right trigger (0-255)
 *   [6-7]   Left stick X (signed 16-bit, little-endian)
 *   [8-9]   Left stick Y
 *   [10-11] Right stick X
 *   [12-13] Right stick Y
 *   ...     (remaining bytes vary)
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "usb/usb_host.h"

#include "xbox_receiver.h"

static const char *TAG = "xbox_receiver";

// USB Host state
static usb_host_client_handle_t s_client_hdl = NULL;
static usb_device_handle_t s_device_hdl = NULL;
static bool s_receiver_connected = false;
static uint8_t s_device_addr = 0;

// Controller states
static xbox_controller_state_t s_controller_state[XBOX_SLOT_MAX];
static SemaphoreHandle_t s_state_mutex;

// User callback
static xbox_state_callback_t s_user_callback = NULL;

// Transfer buffer for IN endpoint
static usb_transfer_t *s_in_xfer = NULL;
static uint8_t s_ep_in_addr = 0;
static usb_transfer_t *s_out_xfer = NULL;
static uint8_t s_ep_out_addr = 0;

// Device management task
static TaskHandle_t s_device_task = NULL;
static uint8_t s_pending_dev_addr = 0;
static SemaphoreHandle_t s_device_sem = NULL;
static bool s_opening_device = false;
static volatile bool s_device_gone = false;

// Button bit positions in the report
#define BTN_DPAD_UP     0x0001
#define BTN_DPAD_DOWN   0x0002
#define BTN_DPAD_LEFT   0x0004
#define BTN_DPAD_RIGHT  0x0008
#define BTN_START       0x0010
#define BTN_BACK        0x0020
#define BTN_LEFT_STICK  0x0040
#define BTN_RIGHT_STICK 0x0080
#define BTN_LB          0x0100
#define BTN_RB          0x0200
#define BTN_GUIDE       0x0400
#define BTN_A           0x1000
#define BTN_B           0x2000
#define BTN_X           0x4000
#define BTN_Y           0x8000

static void out_xfer_cb(usb_transfer_t *xfer);  // Forward declaration

static volatile bool s_out_pending = false;

/**
 * Send LED command to set player indicator
 */
static void send_player_led(int player)
{
    if (!s_out_xfer || !s_device_hdl || s_out_pending) {
        return;
    }
    
    // LED command: 0x40 | pattern
    // pattern 2-5 = flash then solid for player 1-4
    // pattern 6-9 = solid immediately for player 1-4
    uint8_t pattern = 0x40 | (player + 2);  // player 0 -> pattern 2 (flash then P1)
    uint8_t led_cmd[] = {0x00, 0x00, 0x08, pattern, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    memcpy(s_out_xfer->data_buffer, led_cmd, sizeof(led_cmd));
    s_out_xfer->num_bytes = 12;
    
    s_out_pending = true;
    esp_err_t err = usb_host_transfer_submit(s_out_xfer);
    if (err != ESP_OK) {
        s_out_pending = false;
        ESP_LOGW(TAG, "LED command failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Sent player %d LED command", player + 1);
    }
}

/**
 * Parse controller data from USB report
 * 
 * Xbox 360 wireless racing wheel format (29 bytes):
 *   [0]     Always 0x00
 *   [1]     Data indicator: 0x01 = input data, 0x00 = idle/keepalive
 *   [2]     Unknown (0x00)
 *   [3]     Header byte (0xf0 or 0x80)
 *   [4]     Unknown
 *   [5]     Input flags: 0x02 = has input
 *   [6-9]   Buttons/triggers (needs mapping)
 *   [10-11] Wheel position (16-bit little-endian, ~0x0000-0xFFFF)
 *   [12+]   Other data
 */
static void parse_controller_report(xbox_slot_t slot, const uint8_t *data, size_t len)
{
    // Check for connection status packets (2 bytes)
    if (len == 2 && data[0] == 0x08) {
        // 08 80 = controller connected notification
        ESP_LOGI(TAG, "Controller %d connect notification", slot);
        send_player_led(slot);
        return;
    }
    
    if (len < 12) {
        return;
    }
    
    // Skip non-input packets (capability queries, etc)
    // Input packets have: data[0]==0x00, data[3]==0xf0 or 0x80
    if (data[0] != 0x00 || (data[3] != 0xf0 && data[3] != 0x80)) {
        return;
    }
    
    // data[1] == 0x00 means idle/keepalive, 0x01 means actual input
    if (data[1] != 0x01) {
        return;
    }
    
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }
    
    xbox_controller_state_t *state = &s_controller_state[slot];
    bool was_connected = state->connected;
    state->connected = true;
    
    // Wheel position at bytes 10-11 (little-endian unsigned 16-bit)
    // Raw wheel reports inverted magnitude: center=0x0000, full turn approaches 0x8000
    // Normalize to standard axis: center=0, left=negative, right=positive
    uint16_t wheel_raw = data[10] | (data[11] << 8);
    int16_t wheel_signed = (int16_t)(wheel_raw - 0x8000);
    if (wheel_signed >= 0) {
        state->left_stick_x = 32767 - wheel_signed;
    } else {
        state->left_stick_x = -32767 - wheel_signed;
    }
    
    // Buttons appear to be at bytes 6-7 for the wheel
    uint16_t buttons = data[6] | (data[7] << 8);
    state->buttons.a           = (buttons & 0x1000) != 0;
    state->buttons.b           = (buttons & 0x2000) != 0;
    state->buttons.x           = (buttons & 0x4000) != 0;
    state->buttons.y           = (buttons & 0x8000) != 0;
    state->buttons.lb          = (buttons & 0x0100) != 0;
    state->buttons.rb          = (buttons & 0x0200) != 0;
    state->buttons.back        = (buttons & 0x0020) != 0;
    state->buttons.start       = (buttons & 0x0010) != 0;
    state->buttons.dpad_up     = (buttons & 0x0001) != 0;
    state->buttons.dpad_down   = (buttons & 0x0002) != 0;
    state->buttons.dpad_left   = (buttons & 0x0004) != 0;
    state->buttons.dpad_right  = (buttons & 0x0008) != 0;
    
    // Triggers at bytes 8-9 (need to verify with your wheel)
    state->left_trigger  = data[8];
    state->right_trigger = data[9];
    
    // Clear unused axes
    state->left_stick_y  = 0;
    state->right_stick_x = 0;
    state->right_stick_y = 0;
    
    xbox_controller_state_t callback_copy;
    memcpy(&callback_copy, state, sizeof(xbox_controller_state_t));
    xSemaphoreGive(s_state_mutex);

    if (!was_connected) {
        ESP_LOGI(TAG, "Controller %d connected", slot);
    }

    if (s_user_callback) {
        s_user_callback(slot, &callback_copy);
    }
}

/**
 * OUT transfer callback
 */
static void out_xfer_cb(usb_transfer_t *xfer)
{
    s_out_pending = false;
    if (xfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGW(TAG, "OUT xfer status: %d", xfer->status);
    }
}

/**
 * IN transfer callback
 */
static void in_xfer_cb(usb_transfer_t *xfer)
{
    if (xfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        if (xfer->actual_num_bytes > 0) {
            parse_controller_report(XBOX_SLOT_1, xfer->data_buffer, xfer->actual_num_bytes);
        }
    } else if (xfer->status == USB_TRANSFER_STATUS_NO_DEVICE) {
        ESP_LOGW(TAG, "Device gone during transfer");
        return;  // Don't resubmit, wait for DEV_GONE event
    } else if (xfer->status != USB_TRANSFER_STATUS_CANCELED) {
        ESP_LOGW(TAG, "IN xfer status: %d", xfer->status);
        // For other errors, add a small delay before retry
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Resubmit if still connected
    if (s_receiver_connected && s_device_hdl && !s_device_gone) {
        esp_err_t err = usb_host_transfer_submit(xfer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to resubmit IN xfer: %s", esp_err_to_name(err));
            // Don't call close_device here, let DEV_GONE handle it
        }
    }
}

/**
 * Open device and start transfers
 */
static void open_device(uint8_t dev_addr)
{
    esp_err_t err;
    
    s_opening_device = true;
    s_ep_in_addr = 0;
    s_device_gone = false;
    
    err = usb_host_device_open(s_client_hdl, dev_addr, &s_device_hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open device: %s", esp_err_to_name(err));
        s_opening_device = false;
        return;
    }
    
    // Full 5-second stability wait like the test that worked
    ESP_LOGI(TAG, "Device opened, waiting 5s for stability...");
    for (int i = 0; i < 10; i++) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (s_device_gone) {
            ESP_LOGW(TAG, "Device lost at %dms", (i+1) * 500);
            s_device_hdl = NULL;
            s_opening_device = false;
            return;
        }
    }
    
    ESP_LOGI(TAG, "Device stable, getting descriptor...");
    
    const usb_device_desc_t *dev_desc;
    err = usb_host_get_device_descriptor(s_device_hdl, &dev_desc);
    if (err != ESP_OK || s_device_gone) {
        ESP_LOGE(TAG, "Failed to get descriptor");
        goto fail_close;
    }
    
    ESP_LOGI(TAG, "VID=0x%04x PID=0x%04x", dev_desc->idVendor, dev_desc->idProduct);
    
    if (dev_desc->idVendor != XBOX_RECEIVER_VID || dev_desc->idProduct != XBOX_RECEIVER_PID) {
        goto fail_close;
    }
    
    const usb_config_desc_t *config_desc;
    err = usb_host_get_active_config_descriptor(s_device_hdl, &config_desc);
    if (err != ESP_OK || s_device_gone) {
        ESP_LOGE(TAG, "Failed to get config descriptor");
        goto fail_close;
    }
    
    // Find endpoint
    const uint8_t *p = (const uint8_t *)config_desc;
    int offset = 0;
    while (offset < config_desc->wTotalLength) {
        const usb_standard_desc_t *desc = (const usb_standard_desc_t *)(p + offset);
        if (desc->bLength == 0) break;
        if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
            const usb_ep_desc_t *ep = (const usb_ep_desc_t *)desc;
            if ((ep->bmAttributes & 0x03) == 0x03) {  // Interrupt endpoint
                if ((ep->bEndpointAddress & 0x80) && s_ep_in_addr == 0) {
                    s_ep_in_addr = ep->bEndpointAddress;
                    ESP_LOGI(TAG, "Found IN endpoint: 0x%02x", s_ep_in_addr);
                } else if (!(ep->bEndpointAddress & 0x80) && s_ep_out_addr == 0) {
                    s_ep_out_addr = ep->bEndpointAddress;
                    ESP_LOGI(TAG, "Found OUT endpoint: 0x%02x", s_ep_out_addr);
                }
            }
        }
        offset += desc->bLength;
    }
    
    if (s_ep_in_addr == 0 || s_ep_out_addr == 0 || s_device_gone) {
        ESP_LOGE(TAG, "Missing endpoints (in=0x%02x out=0x%02x) or device gone", s_ep_in_addr, s_ep_out_addr);
        goto fail_close;
    }
    
    ESP_LOGI(TAG, "Using endpoints IN=0x%02x OUT=0x%02x, waiting before claim...", s_ep_in_addr, s_ep_out_addr);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    if (s_device_gone) {
        ESP_LOGW(TAG, "Device gone before claim");
        goto fail_close;
    }
    
    err = usb_host_interface_claim(s_client_hdl, s_device_hdl, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Claim failed: %s", esp_err_to_name(err));
        goto fail_close;
    }
    
    ESP_LOGI(TAG, "Claimed! Allocating transfer...");
    
    err = usb_host_transfer_alloc(32, 0, &s_in_xfer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Alloc failed");
        goto fail_release;
    }
    
    s_in_xfer->device_handle = s_device_hdl;
    s_in_xfer->bEndpointAddress = s_ep_in_addr;
    s_in_xfer->callback = in_xfer_cb;
    s_in_xfer->context = NULL;
    s_in_xfer->num_bytes = 32;
    
    err = usb_host_transfer_submit(s_in_xfer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Submit failed");
        goto fail_free;
    }
    
    // Allocate OUT transfer for commands
    err = usb_host_transfer_alloc(12, 0, &s_out_xfer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OUT alloc failed");
        goto fail_free;
    }
    
    s_out_xfer->device_handle = s_device_hdl;
    s_out_xfer->bEndpointAddress = s_ep_out_addr;
    s_out_xfer->callback = out_xfer_cb;
    s_out_xfer->context = NULL;
    
    // Send initial LED command (in case controller is already on)
    send_player_led(XBOX_SLOT_1);
    
    s_device_addr = dev_addr;
    s_receiver_connected = true;
    s_opening_device = false;
    ESP_LOGI(TAG, "Xbox receiver ready!");
    return;

fail_free:
    usb_host_transfer_free(s_in_xfer);
    s_in_xfer = NULL;
fail_release:
    usb_host_interface_release(s_client_hdl, s_device_hdl, 0);
fail_close:
    if (s_device_hdl && !s_device_gone) {
        usb_host_device_close(s_client_hdl, s_device_hdl);
    }
    s_device_hdl = NULL;
    s_ep_in_addr = 0;
    s_opening_device = false;
}

/**
 * Close device and cleanup
 */
static void close_device(bool device_gone)
{
    s_receiver_connected = false;

    // If open_device is running, it will see s_device_gone and abort
    if (s_opening_device) {
        ESP_LOGD(TAG, "close_device while opening, flag set");
        return;  // open_device will clean up
    }
    
    if (s_in_xfer) {
        // Cancel pending transfer if device still there
        if (!device_gone && s_device_hdl) {
            usb_host_endpoint_halt(s_device_hdl, s_ep_in_addr);
            usb_host_endpoint_flush(s_device_hdl, s_ep_in_addr);
        }
        usb_host_transfer_free(s_in_xfer);
        s_in_xfer = NULL;
    }
    
    if (s_out_xfer) {
        usb_host_transfer_free(s_out_xfer);
        s_out_xfer = NULL;
        s_out_pending = false;
    }
    
    if (s_device_hdl) {
        if (!device_gone) {
            usb_host_interface_release(s_client_hdl, s_device_hdl, 0);
            usb_host_device_close(s_client_hdl, s_device_hdl);
        }
        s_device_hdl = NULL;
    }
    
    s_ep_in_addr = 0;
    s_ep_out_addr = 0;
    s_device_addr = 0;
    
    // Mark all controllers disconnected
    for (int i = 0; i < XBOX_SLOT_MAX; i++) {
        if (s_controller_state[i].connected) {
            s_controller_state[i].connected = false;
            if (s_user_callback) {
                xbox_controller_state_t copy = s_controller_state[i];
                s_user_callback(i, &copy);
            }
        }
    }
}

/**
 * Task that handles device open/close (not in callback context)
 */
static void device_task(void *pvParameters)
{
    while (1) {
        // Wait for signal to open device
        if (xSemaphoreTake(s_device_sem, portMAX_DELAY) == pdTRUE) {
            if (s_pending_dev_addr != 0 && !s_receiver_connected) {
                open_device(s_pending_dev_addr);
                s_pending_dev_addr = 0;
            }
        }
    }
}

/**
 * USB Host library event handler
 */
static void host_lib_task(void *pvParameters)
{
    while (1) {
        uint32_t event_flags;
        esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "usb_host_lib_handle_events failed: %s", esp_err_to_name(err));
            break;
        }
        
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGD(TAG, "No more USB clients");
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGD(TAG, "All USB devices freed");
        }
    }
    vTaskDelete(NULL);
}

/**
 * USB client event callback
 */
static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            ESP_LOGI(TAG, "New USB device, address: %d", event_msg->new_dev.address);
            s_device_gone = false;
            if (!s_receiver_connected && s_pending_dev_addr == 0) {
                s_pending_dev_addr = event_msg->new_dev.address;
                xSemaphoreGive(s_device_sem);
            }
            break;
            
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            ESP_LOGW(TAG, "USB device disconnected");
            s_device_gone = true;  // Immediate flag
            close_device(true);
            break;
            
        default:
            break;
    }
}

/**
 * Client task
 */
static void client_task(void *pvParameters)
{
    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = NULL,
        },
    };
    
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &s_client_hdl));
    ESP_LOGI(TAG, "USB client registered");
    
    while (1) {
        usb_host_client_handle_events(s_client_hdl, portMAX_DELAY);
    }
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t xbox_receiver_init(xbox_state_callback_t callback)
{
    s_user_callback = callback;
    
    s_state_mutex = xSemaphoreCreateMutex();
    if (s_state_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    s_device_sem = xSemaphoreCreateBinary();
    if (s_device_sem == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    memset(s_controller_state, 0, sizeof(s_controller_state));
    
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install USB host: %s", esp_err_to_name(err));
        return err;
    }
    
    xTaskCreate(host_lib_task, "usb_host_lib", 4096, NULL, 5, NULL);
    xTaskCreate(client_task, "usb_client", 4096, NULL, 5, NULL);
    xTaskCreate(device_task, "usb_device", 4096, NULL, 4, &s_device_task);
    
    ESP_LOGI(TAG, "USB Host initialized, waiting for Xbox receiver...");
    
    return ESP_OK;
}

esp_err_t xbox_receiver_get_state(xbox_slot_t slot, xbox_controller_state_t *state)
{
    if (slot >= XBOX_SLOT_MAX || state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    memcpy(state, &s_controller_state[slot], sizeof(xbox_controller_state_t));
    bool connected = state->connected;
    
    xSemaphoreGive(s_state_mutex);
    
    return connected ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t xbox_receiver_set_rumble(xbox_slot_t slot, uint8_t left_motor, uint8_t right_motor)
{
    ESP_LOGW(TAG, "Rumble not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

bool xbox_receiver_is_connected(void)
{
    return s_receiver_connected;
}
