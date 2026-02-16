/**
 * Fuzz harness for CRSF pack_channels().
 *
 * Feeds arbitrary 16-element uint16 arrays and verifies:
 * - No buffer overrun (sentinel bytes)
 * - 11-bit values round-trip correctly
 */

#include "stubs.h"
#include "../main/crsf.h"

/* Shared stub globals */
uint32_t g_tick_count = 0;
uint8_t  g_uart_buf[UART_BUF_SIZE];
size_t   g_uart_len = 0;

/* Include crsf.c to get pack_channels (it's static) */
#include "../main/crsf.c"

/**
 * Unpack 22 bytes back to 16 channels (inverse of pack_channels)
 */
static void unpack_channels(const uint8_t *packed, crsf_channels_t *channels) {
    channels->ch[0]  = (uint16_t)((packed[0]       | (packed[1]  << 8)) & 0x07FF);
    channels->ch[1]  = (uint16_t)(((packed[1] >> 3) | (packed[2]  << 5)) & 0x07FF);
    channels->ch[2]  = (uint16_t)(((packed[2] >> 6) | (packed[3]  << 2) | (packed[4] << 10)) & 0x07FF);
    channels->ch[3]  = (uint16_t)(((packed[4] >> 1) | (packed[5]  << 7)) & 0x07FF);
    channels->ch[4]  = (uint16_t)(((packed[5] >> 4) | (packed[6]  << 4)) & 0x07FF);
    channels->ch[5]  = (uint16_t)(((packed[6] >> 7) | (packed[7]  << 1) | (packed[8] << 9)) & 0x07FF);
    channels->ch[6]  = (uint16_t)(((packed[8] >> 2) | (packed[9]  << 6)) & 0x07FF);
    channels->ch[7]  = (uint16_t)(((packed[9] >> 5) | (packed[10] << 3)) & 0x07FF);
    channels->ch[8]  = (uint16_t)((packed[11]       | (packed[12] << 8)) & 0x07FF);
    channels->ch[9]  = (uint16_t)(((packed[12] >> 3) | (packed[13] << 5)) & 0x07FF);
    channels->ch[10] = (uint16_t)(((packed[13] >> 6) | (packed[14] << 2) | (packed[15] << 10)) & 0x07FF);
    channels->ch[11] = (uint16_t)(((packed[15] >> 1) | (packed[16] << 7)) & 0x07FF);
    channels->ch[12] = (uint16_t)(((packed[16] >> 4) | (packed[17] << 4)) & 0x07FF);
    channels->ch[13] = (uint16_t)(((packed[17] >> 7) | (packed[18] << 1) | (packed[19] << 9)) & 0x07FF);
    channels->ch[14] = (uint16_t)(((packed[19] >> 2) | (packed[20] << 6)) & 0x07FF);
    channels->ch[15] = (uint16_t)(((packed[20] >> 5) | (packed[21] << 3)) & 0x07FF);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < sizeof(crsf_channels_t)) return 0;

    crsf_channels_t input;
    memcpy(&input, data, sizeof(input));

    /* Mask to 11-bit values */
    for (int i = 0; i < CRSF_NUM_CHANNELS; i++) {
        input.ch[i] &= 0x07FF;
    }

    /* Pack into buffer with sentinels to detect overrun */
    uint8_t buf[24];
    buf[0] = 0xDE;           /* sentinel before */
    buf[23] = 0xAD;          /* sentinel after */
    pack_channels(&input, &buf[1]);

    /* Sentinels must be intact */
    if (buf[0] != 0xDE || buf[23] != 0xAD) {
        __builtin_trap();
    }

    /* Round-trip: unpack and compare */
    crsf_channels_t output;
    unpack_channels(&buf[1], &output);

    for (int i = 0; i < CRSF_NUM_CHANNELS; i++) {
        if (input.ch[i] != output.ch[i]) {
            __builtin_trap();
        }
    }

    return 0;
}
