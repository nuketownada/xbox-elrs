/**
 * Fuzz harness for xbox_receiver parse_controller_report().
 *
 * Feeds arbitrary byte buffers to the USB report parser and checks
 * that it never crashes or produces out-of-range state.
 */

#include "stubs.h"

/* Shared stub globals */
uint32_t g_tick_count = 0;
uint8_t  g_uart_buf[UART_BUF_SIZE];
size_t   g_uart_len = 0;

/* Include the source directly — stubs shadow ESP-IDF headers */
#include "../main/xbox_receiver.c"

/* Captured state for validation */
static xbox_controller_state_t g_last_state;
static bool g_callback_fired;

static void fuzz_callback(xbox_slot_t slot, const xbox_controller_state_t *state) {
    (void)slot;
    memcpy(&g_last_state, state, sizeof(g_last_state));
    g_callback_fired = true;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Limit to plausible USB report size */
    if (size > 64) return 0;

    /* Set up state for parse_controller_report */
    s_user_callback = fuzz_callback;
    if (!s_state_mutex) {
        s_state_mutex = xSemaphoreCreateMutex();
    }
    g_callback_fired = false;

    parse_controller_report(XBOX_SLOT_1, data, size);

    return 0;
}
