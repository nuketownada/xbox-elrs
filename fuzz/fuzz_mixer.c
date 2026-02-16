/**
 * Fuzz harness for channel_mixer mixer_process().
 *
 * Feeds arbitrary xbox_controller_state_t to the mixer and verifies
 * all output channels are within CRSF bounds.
 */

#include "stubs.h"

/* Shared stub globals */
uint32_t g_tick_count = 0;
uint8_t  g_uart_buf[UART_BUF_SIZE];
size_t   g_uart_len = 0;

/* Include the mixer source directly (stubs shadow ESP-IDF) */
#include "../main/channel_mixer.c"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < sizeof(xbox_controller_state_t)) return 0;

    /* Initialize mixer once */
    static bool inited = false;
    if (!inited) {
        mixer_config_t cfg = MIXER_CONFIG_DEFAULT();
        mixer_init(&cfg);
        inited = true;
    }

    /* Construct state from fuzz input */
    xbox_controller_state_t state;
    memcpy(&state, data, sizeof(state));

    crsf_channels_t out;
    mixer_process(&state, &out);

    /* All channels must be in valid CRSF range */
    for (int i = 0; i < CRSF_NUM_CHANNELS; i++) {
        if (out.ch[i] < CRSF_CHANNEL_MIN || out.ch[i] > CRSF_CHANNEL_MAX) {
            __builtin_trap();
        }
    }

    /* When disconnected: throttle at MIN, arm at MIN */
    if (!state.connected) {
        if (out.ch[RC_CH_THROTTLE] != CRSF_CHANNEL_MIN) {
            __builtin_trap();
        }
        /* arm channel from default config */
        if (out.ch[RC_CH_AUX1] != CRSF_CHANNEL_MIN) {
            __builtin_trap();
        }
    }

    return 0;
}
