#ifndef CHASSIS_PROTOCOL_H
#define CHASSIS_PROTOCOL_H

#include <stdint.h>

typedef struct {
    int16_t velocity;
    int16_t angular;
} chassis_state_t;

int chassis_parse(uint8_t byte, chassis_state_t *state);

#endif
