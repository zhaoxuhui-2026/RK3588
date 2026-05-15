#ifndef SERIAL_PORT_H
#define SERIAL_PORT_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int fd;
} serial_t;

int serial_open(serial_t *ser, const char *device, int baudrate);
void serial_close(serial_t *ser);
int serial_read(serial_t *ser, uint8_t *buf, size_t len);
int serial_write(serial_t *ser, const uint8_t *data, size_t len);
void serial_hexdump(const char *prefix, uint8_t *buf, int len);

#endif
