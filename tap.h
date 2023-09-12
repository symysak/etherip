#ifndef TAP_H
#define TAP_H

extern int tap_open(int *fd, char name[], int mtu);
extern int tap_close(int fd);
extern ssize_t tap_read(int fd, uint8_t *buffer, size_t size);
extern ssize_t tap_write(int fd, const uint8_t *frame, size_t flen);

#endif