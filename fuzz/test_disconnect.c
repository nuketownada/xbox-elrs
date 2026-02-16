/**
 * Deterministic disconnect notification test.
 *
 * Exercises the Xbox wireless disconnect notification path (0x08 0x00)
 * which replaced the staleness watchdog approach.
 *
 * Not a libFuzzer target -- built and run as a regular executable.
 */

#include <assert.h>
#include "stubs.h"
#include "../main/xbox_receiver.h"

/* Shared stub globals */
uint32_t g_tick_count = 0;
uint8_t  g_uart_buf[UART_BUF_SIZE];
size_t   g_uart_len = 0;

/* Include xbox_receiver.c for access to parse_controller_report */
#include "../main/xbox_receiver.c"

/* Enable logging for this test */
#undef ESP_LOGW
#undef ESP_LOGI
#define ESP_LOGW(tag, fmt, ...) fprintf(stderr, "W %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) fprintf(stderr, "I %s: " fmt "\n", tag, ##__VA_ARGS__)

/* Test callback tracking */
static int g_callback_count = 0;
static xbox_controller_state_t g_last_callback_state;
static xbox_slot_t g_last_callback_slot;

static void test_callback(xbox_slot_t slot, const xbox_controller_state_t *state)
{
    g_callback_count++;
    g_last_callback_slot = slot;
    memcpy(&g_last_callback_state, state, sizeof(xbox_controller_state_t));
}

/* Minimal valid input packet: data[0]=0x00, data[1]=0x01, data[3]=0xf0, 29 bytes */
static uint8_t make_input_packet(uint8_t *buf, int16_t wheel, uint8_t throttle, uint8_t brake)
{
    memset(buf, 0, 29);
    buf[0] = 0x00;
    buf[1] = 0x01;       /* input data (not keepalive) */
    buf[3] = 0xf0;       /* header byte */
    buf[5] = 0x02;       /* has input flag */
    /* Wheel position at bytes 10-11 (LE uint16) */
    uint16_t wheel_raw = (uint16_t)((int32_t)wheel + 0x8000);
    buf[10] = wheel_raw & 0xFF;
    buf[11] = (wheel_raw >> 8) & 0xFF;
    /* Triggers at bytes 8-9 */
    buf[8] = brake;
    buf[9] = throttle;
    return 29;
}

int main(void)
{
    fprintf(stderr, "=== Disconnect Notification Test ===\n\n");

    /* ---- Init ---- */
    s_state_mutex = xSemaphoreCreateMutex();
    s_user_callback = test_callback;
    memset(s_controller_state, 0, sizeof(s_controller_state));

    /* ---- Test 1: Connect notification (0x08 0x80) ---- */
    fprintf(stderr, "Test 1: Connect notification does not fire user callback\n");
    g_callback_count = 0;
    uint8_t connect_pkt[] = {0x08, 0x80};
    parse_controller_report(XBOX_SLOT_1, connect_pkt, sizeof(connect_pkt));
    /* Connect notification sends LED command but does not fire user callback */
    assert(g_callback_count == 0);
    fprintf(stderr, "  PASS\n\n");

    /* ---- Test 2: Input data sets connected=true and fires callback ---- */
    fprintf(stderr, "Test 2: Input data sets connected=true\n");
    g_callback_count = 0;
    uint8_t input_pkt[29];
    make_input_packet(input_pkt, 0, 50, 0);  /* center wheel, some throttle */
    parse_controller_report(XBOX_SLOT_1, input_pkt, 29);
    assert(g_callback_count == 1);
    assert(g_last_callback_state.connected == true);
    assert(g_last_callback_state.right_trigger == 50);
    fprintf(stderr, "  PASS\n\n");

    /* ---- Test 3: Disconnect notification fires callback with connected=false ---- */
    fprintf(stderr, "Test 3: Disconnect notification (0x08 0x00)\n");
    g_callback_count = 0;
    uint8_t disconnect_pkt[] = {0x08, 0x00};
    parse_controller_report(XBOX_SLOT_1, disconnect_pkt, sizeof(disconnect_pkt));
    assert(g_callback_count == 1);
    assert(g_last_callback_slot == XBOX_SLOT_1);
    assert(g_last_callback_state.connected == false);
    fprintf(stderr, "  PASS\n\n");

    /* ---- Test 4: Internal state reflects disconnection ---- */
    fprintf(stderr, "Test 4: Controller state is disconnected\n");
    assert(s_controller_state[XBOX_SLOT_1].connected == false);
    fprintf(stderr, "  PASS\n\n");

    /* ---- Test 5: Keepalive (data[1]==0x00) does NOT fire callback ---- */
    fprintf(stderr, "Test 5: Keepalive packet is silently ignored\n");
    /* First reconnect via input */
    parse_controller_report(XBOX_SLOT_1, input_pkt, 29);
    g_callback_count = 0;
    uint8_t keepalive_pkt[29] = {0};
    keepalive_pkt[0] = 0x00;
    keepalive_pkt[1] = 0x00;  /* keepalive, not input */
    keepalive_pkt[3] = 0xf0;
    parse_controller_report(XBOX_SLOT_1, keepalive_pkt, 29);
    assert(g_callback_count == 0);
    fprintf(stderr, "  PASS\n\n");

    /* ---- Test 6: Disconnect when already disconnected is a no-op ---- */
    fprintf(stderr, "Test 6: Disconnect when already disconnected\n");
    /* First disconnect */
    parse_controller_report(XBOX_SLOT_1, disconnect_pkt, sizeof(disconnect_pkt));
    g_callback_count = 0;
    /* Second disconnect -- state is already false, callback still fires
       (dongle may send multiple disconnect packets) */
    parse_controller_report(XBOX_SLOT_1, disconnect_pkt, sizeof(disconnect_pkt));
    assert(g_callback_count == 1);
    assert(g_last_callback_state.connected == false);
    fprintf(stderr, "  PASS\n\n");

    /* ---- Test 7: Short packets (< 2 bytes) are ignored ---- */
    fprintf(stderr, "Test 7: Short packets ignored\n");
    g_callback_count = 0;
    uint8_t short_pkt[] = {0x08};
    parse_controller_report(XBOX_SLOT_1, short_pkt, 1);
    assert(g_callback_count == 0);
    fprintf(stderr, "  PASS\n\n");

    fprintf(stderr, "=== All tests passed ===\n");
    return 0;
}
