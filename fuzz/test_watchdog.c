/**
 * Deterministic watchdog integration test.
 *
 * Exercises CRSF failsafe via the controllable g_tick_count stub.
 * Not a libFuzzer target — built and run as a regular executable.
 */

#include <assert.h>
#include "stubs.h"
#include "../main/crsf.h"

/* Shared stub globals */
uint32_t g_tick_count = 0;
uint8_t  g_uart_buf[UART_BUF_SIZE];
size_t   g_uart_len = 0;

/* Include crsf.c for access to send_channels_frame and internals */
#include "../main/crsf.c"

/* Enable logging for this test so we can verify messages */
#undef ESP_LOGW
#undef ESP_LOGI
#define ESP_LOGW(tag, fmt, ...) fprintf(stderr, "W %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) fprintf(stderr, "I %s: " fmt "\n", tag, ##__VA_ARGS__)

static void assert_channels_match(const crsf_channels_t *expected) {
    /* Read what send_channels_frame would use by unpacking g_uart_buf.
     * But simpler: just check s_failsafe_active and get channels. */
    crsf_channels_t actual;
    crsf_get_channels(&actual);
    /* crsf_get_channels returns s_channels, not what send actually used.
     * Instead we inspect the failsafe_active flag. */
    (void)expected;
    (void)actual;
}

int main(void) {
    fprintf(stderr, "=== Watchdog Integration Test ===\n\n");

    /* ---- Init ---- */
    crsf_config_t cfg = {
        .uart_num = 1,
        .tx_pin = 0,
        .rx_pin = -1,
        .interval_ms = 4,
        .failsafe_timeout_ms = 250,
    };

    g_tick_count = 0;
    esp_err_t err = crsf_init(&cfg);
    assert(err == ESP_OK);

    /* Set up failsafe channels: all MID, throttle MID, arm (ch4) MIN */
    crsf_channels_t failsafe;
    for (int i = 0; i < CRSF_NUM_CHANNELS; i++) failsafe.ch[i] = CRSF_CHANNEL_MID;
    failsafe.ch[4] = CRSF_CHANNEL_MIN;  /* arm channel = disarmed */
    crsf_set_failsafe(&failsafe);

    /* ---- Test 1: No data received yet, failsafe should NOT be active ---- */
    /* (s_ever_updated is false, so watchdog doesn't trigger) */
    fprintf(stderr, "Test 1: No data yet — failsafe inactive\n");
    send_channels_frame();
    assert(!crsf_is_failsafe_active());
    fprintf(stderr, "  PASS\n\n");

    /* ---- Test 2: Send channel data, failsafe should not be active ---- */
    fprintf(stderr, "Test 2: Fresh data — failsafe inactive\n");
    g_tick_count = 100;
    crsf_channels_t live;
    for (int i = 0; i < CRSF_NUM_CHANNELS; i++) live.ch[i] = CRSF_CHANNEL_MID;
    live.ch[2] = 1200;  /* some throttle */
    live.ch[4] = CRSF_CHANNEL_MAX;  /* armed */
    crsf_set_channels(&live);

    send_channels_frame();
    assert(!crsf_is_failsafe_active());
    fprintf(stderr, "  PASS\n\n");

    /* ---- Test 3: Advance past timeout, failsafe should activate ---- */
    fprintf(stderr, "Test 3: Stale data — failsafe active\n");
    g_tick_count = 100 + 251;  /* 251ms past last update */
    send_channels_frame();
    assert(crsf_is_failsafe_active());
    fprintf(stderr, "  PASS\n\n");

    /* ---- Test 4: New data arrives, failsafe should clear ---- */
    fprintf(stderr, "Test 4: Data resumes — failsafe clears\n");
    g_tick_count = 400;
    crsf_set_channels(&live);
    send_channels_frame();
    assert(!crsf_is_failsafe_active());
    fprintf(stderr, "  PASS\n\n");

    /* ---- Test 5: Edge case — exactly at timeout-1, NOT failsafe ---- */
    fprintf(stderr, "Test 5: Exactly timeout-1 — NOT failsafe\n");
    g_tick_count = 500;
    crsf_set_channels(&live);

    g_tick_count = 500 + 249;  /* 249ms = timeout - 1 */
    send_channels_frame();
    assert(!crsf_is_failsafe_active());
    fprintf(stderr, "  PASS\n\n");

    /* ---- Test 6: Exactly at timeout, IS failsafe ---- */
    fprintf(stderr, "Test 6: Exactly at timeout — IS failsafe\n");
    g_tick_count = 500 + 250;
    send_channels_frame();
    assert(crsf_is_failsafe_active());
    fprintf(stderr, "  PASS\n\n");

    fprintf(stderr, "=== All tests passed ===\n");
    return 0;
}
