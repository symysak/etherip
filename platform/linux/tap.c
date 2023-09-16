#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <linux/if.h>
#include <linux/if_tun.h>


#include "tap.h"
#include "etherip.h"

extern int tap_open(int *fd, char name[], int mtu, int domain){
    *fd = open("/dev/net/tun", O_RDWR);
    
    if(*fd == -1){
        fprintf(stderr, "[ERROR]: Failed to open tap: %s\n", strerror(errno));
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name, IFNAMSIZ);
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    if(ioctl(*fd, TUNSETIFF, &ifr) == -1){
        fprintf(stderr, "[ERROR]: Failed to TUNSETIFF: %s\n", strerror(errno));
        close(*fd);
        return -1;
    }

    if(domain == AF_INET){
        ifr.ifr_mtu = mtu - IPv4_HEADER_LEN - ETHERIP_HEADER_LEN - ETHER_HEADER_LEN;
    }
    else if(domain == AF_INET6){
        ifr.ifr_mtu = mtu - IPv6_HEADER_LEN - ETHERIP_HEADER_LEN - ETHER_HEADER_LEN;
    }
    if(ioctl(socket(AF_INET, SOCK_DGRAM, 0), SIOCSIFMTU, &ifr) == -1){
        fprintf(stderr, "[ERROR]: Failed to SIOCSIFMTU: %s\n", strerror(errno));
        close(*fd);
        return -1;
    }
    
    return 0;
}

extern int tap_close(int fd){
    close(fd);
    return 0;
}

extern ssize_t tap_read(int fd, uint8_t *buffer, size_t size){
    ssize_t len;

    len = read(fd, buffer, size);
    if(len <= 0){
        fprintf(stderr, "[ERROR]: tap_read: %s", strerror(errno));
        return -1;
    }
    return len;
}

extern ssize_t tap_write(int fd, const uint8_t *frame, size_t flen){
    ssize_t len;
    len = write(fd, frame, flen);
    if(len <= 0){
        fprintf(stderr, "[ERROR]: tap_write: %s", strerror(errno));
        return -1;
    }
    return len;
}