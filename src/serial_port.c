#include "serial_port.h"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "log.h"

static int set_baud(int baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 115200: return B115200;
        default:     return B115200;
    }
}

int serial_open(serial_t *ser, const char *device, int baudrate) {
    ser->fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (ser->fd < 0) {
        perror("open serial");
        return -1;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    tcgetattr(ser->fd, &tty);
    cfsetispeed(&tty, set_baud(baudrate));
    cfsetospeed(&tty, set_baud(baudrate));

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;

    tty.c_lflag = 0;
    tty.c_iflag = 0;
    tty.c_oflag = 0;

    tcflush(ser->fd, TCIFLUSH);
    tcsetattr(ser->fd, TCSANOW, &tty);

    return 0;
}

void serial_close(serial_t *ser) {
    if (ser->fd >= 0) {
        close(ser->fd);
        ser->fd = -1;
    }
}

int serial_read(serial_t *ser, uint8_t *buf, size_t len) {
    int serial = read(ser->fd, buf, len);
    return serial;
}

int serial_write(serial_t *ser, const uint8_t *data, size_t len) {
    int serial = write(ser->fd, data, len);
    return serial;
}
