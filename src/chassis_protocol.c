#include "chassis_protocol.h"

static uint8_t rx_buf[64];
static int idx = 0;

int chassis_parse(uint8_t byte, chassis_state_t *state) {
    rx_buf[idx++] = byte;

    // 协议帧头：0xAA 0x54 
    if (idx == 6 && rx_buf[0] == 0xAA && rx_buf[1] == 0x54) {
        state->velocity = ((int16_t)rx_buf[1] << 8) | rx_buf[2];
        state->angular  = ((int16_t)rx_buf[3] << 8) | rx_buf[4];
        idx = 0;
        return 1;
    }

    if (idx >= 64) {
        idx = 0;
    }
    return 0;
}
