#ifndef SOCKET_H
#define SOCKET_H

#include <sys/socket.h>
#include <netinet/ip.h>

extern int sock_open(int *fd, int domain, struct sockaddr *addr, socklen_t addr_len);
extern int sock_close(int fd);
extern ssize_t sock_read(int fd, uint8_t *buffer, size_t size, struct sockaddr *addr, socklen_t *addr_len);
extern ssize_t sock_write(int fd, const uint8_t *frame, size_t size, struct sockaddr *addr, socklen_t addr_len);

#endif