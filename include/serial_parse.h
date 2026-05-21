#ifndef SERIAL_PARSE_H
#define SERIAL_PARSE_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    CMD_UNKNOWN = 0,
    CMD_NAV_RESULT,
    CMD_SYS_BOOT,
    CMD_BASE_VEL,
    CMD_CHECK_SENSOR
} chassis_cmd;

int parse_serial_cmd(const char *buf);
bool has_digit_1_to_4(const char *field);
const char *find_target_field(const char *str, size_t len);
int parse_hostname_value(const char *buf, char *out_str);
int parse_base_val_value(const char *buf, int *out_value);
int parse_check_sensors_value(const char *buf, int *out_value);
int parse_nav_result_value(const char *buf, bool *out_ret);

int build_frame(const char *hostname, uint8_t *frame, int max_len);

#endif